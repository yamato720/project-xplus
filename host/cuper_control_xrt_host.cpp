#include "cuper_control_matrix.hpp"
#include "cpu_reference.hpp"
#include "multi_kernel_solver.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

namespace {

using project_xplus::cgsolver::CuperControlMatrix;
using project_xplus::cgsolver::CpuReferenceResult;
using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::SolverConfig;
using project_xplus::cgsolver::data_t;

struct HostOptions {
    std::filesystem::path xclbin_path;
    std::filesystem::path dataset_dir;
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = project_xplus::cgsolver::run_defaults::kMaxIters;
    double diff_tol = 1.0e-3;
    unsigned int device_index = project_xplus::cgsolver::run_defaults::kDeviceIndex;
};

double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

int parse_int(const char* text, const char* name) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

unsigned int parse_uint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [xclbin] [dataset_dir] [--tau value] [--max-iters value]"
              << " [--diff-tol value] [--device-index value]\n";
}

HostOptions parse_args(int argc, char** argv) {
    HostOptions options;
    options.xclbin_path =
        project_xplus::cgsolver::run_defaults::project_root(argv[0]) / "build" /
        project_xplus::cgsolver::run_defaults::xrt_target() / "cuper_pcg_control_kernel.xclbin";
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.xclbin_path = std::filesystem::path(argv[index++]);
    }
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = std::filesystem::path(argv[index++]);
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--tau") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--tau requires a value");
            }
            options.tau = parse_double(argv[++index], "tau");
        } else if (arg == "--max-iters") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-iters requires a value");
            }
            options.max_iters = parse_int(argv[++index], "max_iters");
        } else if (arg == "--diff-tol") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--diff-tol requires a value");
            }
            options.diff_tol = parse_double(argv[++index], "diff_tol");
        } else if (arg == "--device-index") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--device-index requires a value");
            }
            options.device_index = parse_uint(argv[++index], "device_index");
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.tau <= 0.0) {
        throw std::runtime_error("--tau must be positive");
    }
    if (options.max_iters < 0) {
        throw std::runtime_error("--max-iters must be non-negative");
    }
    if (options.diff_tol <= 0.0) {
        throw std::runtime_error("--diff-tol must be positive");
    }

    return options;
}

template <typename T>
xrt::bo make_input_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
xrt::bo make_inout_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, std::vector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const HostOptions options = parse_args(argc, argv);
        const Dataset dataset = Dataset::load(options.dataset_dir);
        const CuperControlMatrix matrix =
            project_xplus::cgsolver::build_cuper_control_matrix(dataset);

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;
        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        std::cout << "[xplus-xrt] xclbin=" << options.xclbin_path
                  << " dataset=" << options.dataset_dir
                  << " kernel=cuper_pcg_control_kernel"
                  << " spmv=cuper-column-batch-row-tile"
                  << " batches=" << matrix.batch_count
                  << " row_tiles=" << matrix.row_tile_count << "\n";

        xrt::device device(options.device_index);
        auto uuid = device.load_xclbin(options.xclbin_path.string());
        xrt::kernel kernel(device, uuid, "cuper_pcg_control_kernel");

        std::vector<data_t> x = dataset.x0();
        std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> ap(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> m_inv = project_xplus::cgsolver::build_jacobi_inverse(dataset);
        std::vector<data_t> metrics(4, 0.0);
        std::vector<int> status(2, 0);

        auto batch_ptr_bo = make_input_bo(device, kernel, 0, matrix.batch_ptr);
        auto batch_tile_ptr_bo = make_input_bo(device, kernel, 1, matrix.batch_tile_ptr);
        auto rows_bo = make_input_bo(device, kernel, 2, matrix.element_rows);
        auto cols_bo = make_input_bo(device, kernel, 3, matrix.element_cols);
        auto vals_bo = make_input_bo(device, kernel, 4, matrix.element_values);
        auto b_bo = make_input_bo(device, kernel, 5, dataset.b());
        auto minv_bo = make_input_bo(device, kernel, 6, m_inv);
        auto x_bo = make_inout_bo(device, kernel, 7, x);
        auto r_bo = make_inout_bo(device, kernel, 8, r);
        auto z_bo = make_inout_bo(device, kernel, 9, z);
        auto p_bo = make_inout_bo(device, kernel, 10, p);
        auto ap_bo = make_inout_bo(device, kernel, 11, ap);
        auto metrics_bo = make_inout_bo(device, kernel, 12, metrics);
        auto status_bo = make_inout_bo(device, kernel, 13, status);

        auto run = kernel(batch_ptr_bo,
                          batch_tile_ptr_bo,
                          rows_bo,
                          cols_bo,
                          vals_bo,
                          b_bo,
                          minv_bo,
                          x_bo,
                          r_bo,
                          z_bo,
                          p_bo,
                          ap_bo,
                          metrics_bo,
                          status_bo,
                          config.tau,
                          effective_max_iters,
                          dataset.n(),
                          matrix.batch_count);
        run.wait();

        x_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, x.size() * sizeof(data_t), 0);
        metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(data_t), 0);
        status_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, status.size() * sizeof(int), 0);

        const auto x_mapped = x_bo.map<data_t*>();
        const auto metrics_mapped = metrics_bo.map<data_t*>();
        const auto status_mapped = status_bo.map<int*>();
        std::copy(x_mapped, x_mapped + x.size(), x.begin());
        std::copy(metrics_mapped, metrics_mapped + metrics.size(), metrics.begin());
        std::copy(status_mapped, status_mapped + status.size(), status.begin());

        const CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);
        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < x.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double abs_diff = std::fabs(x[index] - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }

        const double residual_l2 = project_xplus::cgsolver::compute_residual_norm(dataset, x);
        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << status[1]
                  << " residual_abs=" << residual_l2
                  << " status=" << (status[0] == 0 ? "converged"
                                   : status[0] == 1 ? "max_iter"
                                                    : "breakdown")
                  << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_control_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol
                  << " rr=" << metrics[1] << "\n";

        if (status[0] != 0) {
            return 2;
        }
        if (max_abs_diff > options.diff_tol && max_rel_diff > options.diff_tol) {
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
