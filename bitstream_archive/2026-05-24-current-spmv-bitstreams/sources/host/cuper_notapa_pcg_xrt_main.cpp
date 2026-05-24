#include "cuper_control_matrix.hpp"
#include "cuper_pcg_solver.hpp"
#include "cpu_reference.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <array>
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

// 主线 2：no-TAPA Cuper / single SpMV。
//
// 本文件使用 XRT 直接启动 kernels/cuper_pcg_control_kernel.cpp 里的
// cuper_packed_spmv_kernel。该 kernel 只做一次 Cuper packed SpMV，
// 不在 FPGA 内执行 PCG 控制。
//
// 默认模式还保留一个兼容分支：host 侧跑 PCG 主循环，每轮调用同一个
// no-TAPA SpMV kernel。这个分支用于对照和兼容，不是 full-FPGA-PCG 主线。
namespace {

using project_xplus::cgsolver::CuperControlMatrix;
using project_xplus::cgsolver::CuperPcgBackendInfo;
using project_xplus::cgsolver::CuperPcgResult;
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
    bool spmv_only = false;
    int spmv_repeats = 1;
};

struct SpmvXrtTiming {
    // 这些字段把“矩阵打包 / XRT 初始化 / BO 分配 / 单次 kernel”
    // 拆开计时，避免把 setup 成本误当成 SpMV 吞吐。
    double prepare_ms = 0.0;
    double xrt_setup_ms = 0.0;
    double bo_setup_ms = 0.0;
    double x_h2d_ms = 0.0;
    double y_d2h_ms = 0.0;
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
              << " [--diff-tol value] [--device-index value]"
              << " [--spmv-only] [--spmv-repeats value]\n";
}

double elapsed_ms(const std::chrono::steady_clock::time_point start,
                  const std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

HostOptions parse_args(int argc, char** argv) {
    HostOptions options;
    // no-TAPA SpMV 主线默认使用独立 build 目录 cuper-pcg-notapa，
    // 避免和默认 build/sw_emu 或 build/hw 互相覆盖。
    options.xclbin_path =
        project_xplus::cgsolver::run_defaults::project_root(argv[0]) / "cuper-pcg-notapa" /
        project_xplus::cgsolver::run_defaults::xrt_target() / "cuper_packed_spmv_kernel.xclbin";
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
        } else if (arg == "--spmv-only") {
            options.spmv_only = true;
        } else if (arg == "--spmv-repeats") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--spmv-repeats requires a value");
            }
            options.spmv_repeats = parse_int(argv[++index], "spmv_repeats");
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
    if (options.spmv_repeats <= 0) {
        throw std::runtime_error("--spmv-repeats must be positive");
    }

    return options;
}

template <typename T>
xrt::bo make_input_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    // 输入 BO 的 arg_index 必须和 cuper_packed_spmv_kernel 的参数顺序一致。
    // connectivity cfg 根据端口名把这些 BO 绑到对应 HBM bank。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
xrt::bo make_inout_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, std::vector<T>& data) {
    // x/y BO 需要反复复用：spmv-only 会重复写 x、读 y；
    // host-PCG 兼容分支会在每轮 PCG 里用同一组 BO 做 A*p。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = std::chrono::steady_clock::now();
        const HostOptions options = parse_args(argc, argv);

        const auto prepare_start = std::chrono::steady_clock::now();
        const Dataset dataset = Dataset::load(options.dataset_dir);
        // host 端把 CSR 转成 no-TAPA kernel 能直接读取的 Cuper packed 16-HBM 格式。
        // 这个转换成本单独记录到 plan/prepare，不计入单次 SpMV kernel 时间。
        const CuperControlMatrix matrix =
            project_xplus::cgsolver::build_cuper_control_matrix(dataset);
        const auto prepare_end = std::chrono::steady_clock::now();

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        std::cout << "[xplus-xrt] xclbin=" << options.xclbin_path
                  << " dataset=" << options.dataset_dir
                  << " mode="
                  << (options.spmv_only ? "cuper-spmv-notapa-xrt" : "cuper-pcg-notapa-host-pcg")
                  << " kernel=cuper_packed_spmv_kernel"
                  << " spmv=cuper-packed-16hbm"
                  << " batches=" << matrix.batch_count
                  << " matrix_len=" << matrix.matrix_len << "\n";

        SpmvXrtTiming xrt_timing;
        xrt_timing.prepare_ms = elapsed_ms(prepare_start, prepare_end);

        const auto xrt_setup_start = std::chrono::steady_clock::now();
        xrt::device device(options.device_index);
        auto uuid = device.load_xclbin(options.xclbin_path.string());
        xrt::kernel kernel(device, uuid, "cuper_packed_spmv_kernel");
        const auto xrt_setup_end = std::chrono::steady_clock::now();
        xrt_timing.xrt_setup_ms = elapsed_ms(xrt_setup_start, xrt_setup_end);

        std::vector<data_t> x(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> y(static_cast<std::size_t>(dataset.n()), 0.0);

        const auto bo_setup_start = std::chrono::steady_clock::now();
        auto sp_ptr_bo = make_input_bo(device, kernel, 0, matrix.sp_element_list_ptr);
        auto matrix0_bo = make_input_bo(device, kernel, 1, matrix.matrix_data[0]);
        auto matrix1_bo = make_input_bo(device, kernel, 2, matrix.matrix_data[1]);
        auto matrix2_bo = make_input_bo(device, kernel, 3, matrix.matrix_data[2]);
        auto matrix3_bo = make_input_bo(device, kernel, 4, matrix.matrix_data[3]);
        auto matrix4_bo = make_input_bo(device, kernel, 5, matrix.matrix_data[4]);
        auto matrix5_bo = make_input_bo(device, kernel, 6, matrix.matrix_data[5]);
        auto matrix6_bo = make_input_bo(device, kernel, 7, matrix.matrix_data[6]);
        auto matrix7_bo = make_input_bo(device, kernel, 8, matrix.matrix_data[7]);
        auto matrix8_bo = make_input_bo(device, kernel, 9, matrix.matrix_data[8]);
        auto matrix9_bo = make_input_bo(device, kernel, 10, matrix.matrix_data[9]);
        auto matrix10_bo = make_input_bo(device, kernel, 11, matrix.matrix_data[10]);
        auto matrix11_bo = make_input_bo(device, kernel, 12, matrix.matrix_data[11]);
        auto matrix12_bo = make_input_bo(device, kernel, 13, matrix.matrix_data[12]);
        auto matrix13_bo = make_input_bo(device, kernel, 14, matrix.matrix_data[13]);
        auto matrix14_bo = make_input_bo(device, kernel, 15, matrix.matrix_data[14]);
        auto matrix15_bo = make_input_bo(device, kernel, 16, matrix.matrix_data[15]);
        auto x_bo = make_inout_bo(device, kernel, 17, x);
        auto y_bo = make_inout_bo(device, kernel, 18, y);
        auto x_mapped = x_bo.map<data_t*>();
        auto y_mapped = y_bo.map<data_t*>();
        const auto bo_setup_end = std::chrono::steady_clock::now();
        xrt_timing.bo_setup_ms = elapsed_ms(bo_setup_start, bo_setup_end);

        auto run_spmv = [&](const std::vector<data_t>& input,
                            std::vector<data_t>& output,
                            CuperPcgResult& result) {
            // 这里是两种模式共用的最小 SpMV 调用单元：
            //   input  -> x_bo -> cuper_packed_spmv_kernel -> y_bo -> output
            // result.timing.spmv_ms 只累计 kernel run.wait() 包住的时间。
            std::copy(input.begin(), input.end(), x_mapped);

            const auto h2d_start = std::chrono::steady_clock::now();
            x_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, input.size() * sizeof(data_t), 0);
            const auto h2d_end = std::chrono::steady_clock::now();

            const auto kernel_start = std::chrono::steady_clock::now();
            auto run = kernel(sp_ptr_bo,
                              matrix0_bo,
                              matrix1_bo,
                              matrix2_bo,
                              matrix3_bo,
                              matrix4_bo,
                              matrix5_bo,
                              matrix6_bo,
                              matrix7_bo,
                              matrix8_bo,
                              matrix9_bo,
                              matrix10_bo,
                              matrix11_bo,
                              matrix12_bo,
                              matrix13_bo,
                              matrix14_bo,
                              matrix15_bo,
                              x_bo,
                              y_bo,
                              matrix.batch_count,
                              dataset.n());
            run.wait();
            const auto kernel_end = std::chrono::steady_clock::now();

            const auto d2h_start = std::chrono::steady_clock::now();
            y_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, output.size() * sizeof(data_t), 0);
            const auto d2h_end = std::chrono::steady_clock::now();

            output.assign(static_cast<std::size_t>(dataset.n()), 0.0);
            std::copy(y_mapped, y_mapped + output.size(), output.begin());

            xrt_timing.x_h2d_ms += elapsed_ms(h2d_start, h2d_end);
            xrt_timing.y_d2h_ms += elapsed_ms(d2h_start, d2h_end);
            result.timing.spmv_ms += elapsed_ms(kernel_start, kernel_end);
            ++result.timing.spmv_calls;
        };

        if (options.spmv_only) {
            // 主线入口：no-TAPA Cuper single SpMV。
            //
            // 与 TAPA single SpMV host 保持同一口径：输入 dataset.b()，
            // CPU CSR SpMV 做 expected，用 --spmv-repeats 控制重复次数。
            std::vector<data_t> input = dataset.b();
            std::vector<data_t> output(static_cast<std::size_t>(dataset.n()), 0.0);
            std::vector<data_t> expected;
            dataset.spmv(input, expected);

            CuperPcgResult spmv_result;
            spmv_result.timing.plan_ms = xrt_timing.prepare_ms;
            for (int repeat = 0; repeat < options.spmv_repeats; ++repeat) {
                run_spmv(input, output, spmv_result);
            }

            double max_abs_diff = 0.0;
            double max_rel_diff = 0.0;
            for (std::size_t index = 0; index < output.size(); ++index) {
                const double abs_diff = std::fabs(output[index] - expected[index]);
                const double rel_diff = abs_diff / std::max(std::fabs(expected[index]), 1.0e-12);
                max_abs_diff = std::max(max_abs_diff, abs_diff);
                max_rel_diff = std::max(max_rel_diff, rel_diff);
            }
            const auto total_end = std::chrono::steady_clock::now();
            const double kernel_seconds = spmv_result.timing.spmv_ms * 1.0e-3;
            const double gflops = kernel_seconds > 0.0
                                      ? (2.0 * static_cast<double>(dataset.nnz()) *
                                         static_cast<double>(spmv_result.timing.spmv_calls)) /
                                            kernel_seconds / 1.0e9
                                      : 0.0;

            std::cout << std::scientific << std::setprecision(12);
            std::cout << "[done] mode=spmv_only"
                      << " spmv_calls=" << std::defaultfloat << spmv_result.timing.spmv_calls
                      << std::scientific
                      << " status=ok\n";
            std::cout << "[check] max_abs_diff=" << max_abs_diff
                      << " max_rel_diff=" << max_rel_diff
                      << " diff_tol=" << options.diff_tol << "\n";
            std::cout << "[timing-ms] plan=" << spmv_result.timing.plan_ms
                      << " xrt_setup=" << xrt_timing.xrt_setup_ms
                      << " bo_setup=" << xrt_timing.bo_setup_ms
                      << " spmv_total=" << spmv_result.timing.spmv_ms
                      << " spmv_calls=" << std::defaultfloat << spmv_result.timing.spmv_calls
                      << std::scientific
                      << " spmv_avg="
                      << (spmv_result.timing.spmv_calls > 0
                              ? spmv_result.timing.spmv_ms / spmv_result.timing.spmv_calls
                              : 0.0)
                      << " x_h2d=" << xrt_timing.x_h2d_ms
                      << " y_d2h=" << xrt_timing.y_d2h_ms
                      << " total=" << elapsed_ms(total_start, total_end)
                      << " gflops=" << gflops << "\n";

            if (max_abs_diff > options.diff_tol && max_rel_diff > options.diff_tol) {
                return 3;
            }
            return 0;
        }

        CuperPcgBackendInfo backend{
            "notapa-cuper-xrt-fp32-spmv+fp64-host-pcg",
            CuperControlMatrix::kSliceWidth,
            static_cast<std::size_t>(matrix.batch_count),
        };
        // 兼容入口：host-side PCG + no-TAPA Cuper SpMV。
        //
        // 这个分支能复用同一个 cuper_packed_spmv_kernel xclbin，但 PCG 的
        // dot、alpha/beta 和向量更新仍在 host 侧，因此只作为兼容/对照。
        CuperPcgResult cuper_result =
            project_xplus::cgsolver::run_cuper_pcg_solver_with_backend(
                dataset, config, backend, xrt_timing.prepare_ms, run_spmv, &std::cout);

        const auto check_start = std::chrono::steady_clock::now();
        const project_xplus::cgsolver::CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);
        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < cuper_result.solution.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double actual = cuper_result.solution[index];
            const double abs_diff = std::fabs(actual - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }
        const auto check_end = std::chrono::steady_clock::now();
        const auto total_end = std::chrono::steady_clock::now();

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << cuper_result.iterations
                  << " residual_abs=" << cuper_result.residual_l2
                  << " residual_rel=" << cuper_result.residual_rel
                  << " status=" << project_xplus::cgsolver::to_string(cuper_result.status) << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_residual_abs=" << cuper_result.residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol << "\n";
        std::cout << "[timing-ms] plan=" << cuper_result.timing.plan_ms
                  << " xrt_setup=" << xrt_timing.xrt_setup_ms
                  << " bo_setup=" << xrt_timing.bo_setup_ms
                  << " spmv_total=" << cuper_result.timing.spmv_ms
                  << " spmv_calls=" << std::defaultfloat << cuper_result.timing.spmv_calls
                  << std::scientific
                  << " spmv_avg="
                  << (cuper_result.timing.spmv_calls > 0
                          ? cuper_result.timing.spmv_ms / cuper_result.timing.spmv_calls
                          : 0.0)
                  << " x_h2d=" << xrt_timing.x_h2d_ms
                  << " y_d2h=" << xrt_timing.y_d2h_ms
                  << " check=" << elapsed_ms(check_start, check_end)
                  << " total=" << elapsed_ms(total_start, total_end) << "\n";

        if (!cuper_result.converged) {
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
