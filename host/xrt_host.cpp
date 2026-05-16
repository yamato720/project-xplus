#include "cpu_reference.hpp"
#include "dataset_bridge.hpp"
#include "report_io.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

namespace {

using project_xplus::cgsolver::data_t;
using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::IterationTrace;
using project_xplus::cgsolver::KernelTimingStats;
using project_xplus::cgsolver::SolverConfig;

struct HostOptions {
    // XRT 运行需要的 xclbin 路径和数据集路径。
    std::filesystem::path xclbin_path;
    std::filesystem::path dataset_dir;
    // tau 直接对应 rr = r^T r 的阈值。
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = project_xplus::cgsolver::run_defaults::kMaxIters;
    unsigned int device_index = project_xplus::cgsolver::run_defaults::kDeviceIndex;
    // 打开后打印 host / kernel 计时摘要。
    bool timing = project_xplus::cgsolver::run_defaults::kTiming;
    bool verbose = false;
    // 可选的报告输出路径。
    std::filesystem::path json_out;
    std::filesystem::path txt_out;
};

using Clock = std::chrono::steady_clock;

double elapsed_ms(const Clock::time_point begin, const Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

unsigned int parse_uint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

int parse_int(const char* text, const char* name) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [xclbin] [dataset_dir] [--tau value] [--max-iters value] [--device-index value]"
              << " [--timing] [--verbose] [--json-out path] [--txt-out path]\n";
}

HostOptions parse_args(int argc, char** argv) {
    HostOptions options;
    options.xclbin_path = project_xplus::cgsolver::run_defaults::xclbin_path(argv[0]);
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);
    options.json_out = project_xplus::cgsolver::run_defaults::report_json_path(argv[0]);
    options.txt_out = project_xplus::cgsolver::run_defaults::report_text_path(argv[0]);

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
        } else if (arg == "--device-index") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--device-index requires a value");
            }
            options.device_index = parse_uint(argv[++index], "device_index");
        } else if (arg == "--timing") {
            options.timing = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--json-out") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--json-out requires a value");
            }
            options.json_out = std::filesystem::path(argv[++index]);
        } else if (arg == "--txt-out") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--txt-out requires a value");
            }
            options.txt_out = std::filesystem::path(argv[++index]);
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

    return options;
}

template <typename T>
xrt::bo make_input_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    // 对只读输入，host 在创建 BO 时就把内容 map 到 host 侧并同步到 device。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
xrt::bo make_inout_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, std::vector<T>& data) {
    // 对读写缓冲，初始化时先写入默认值，之后由 kernel 在 device 侧原地更新。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

std::vector<data_t> build_jacobi_inverse(const Dataset& dataset) {
    // Jacobi 预条件器不显式构矩阵，只保留 diag(A) 的倒数向量。
    const std::vector<double> diag = dataset.extract_jacobi_diag();
    std::vector<data_t> m_inv(diag.size(), 0.0);
    for (std::size_t index = 0; index < diag.size(); ++index) {
        if (std::fabs(diag[index]) <= project_xplus::cgsolver::kBreakdownEps) {
            throw std::runtime_error("zero diagonal entry while building Jacobi inverse");
        }
        m_inv[index] = 1.0 / diag[index];
    }
    return m_inv;
}

double compute_residual_norm_local(const Dataset& dataset, const std::vector<data_t>& x) {
    // 最终残差用 CPU 侧再算一遍，避免把验证逻辑混进 FPGA kernel。
    std::vector<double> x_as_double(x.begin(), x.end());
    return project_xplus::cgsolver::compute_residual_norm(dataset, x_as_double);
}

}

int main(int argc, char** argv) {
    try {
        const auto total_start = Clock::now();
        const HostOptions options = parse_args(argc, argv);
        const auto host_setup_start = Clock::now();
        const Dataset dataset = Dataset::load(options.dataset_dir);

        if (dataset.n() > project_xplus::cgsolver::kMaxN) {
            throw std::runtime_error("dataset size exceeds kMaxN");
        }

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;
        // 先保留一份 CPU golden，用于最终结果与残差对照。
        const auto golden = project_xplus::cgsolver::run_cpu_reference(dataset, config);

        // 这些向量和标量都对应 Jacobi-PCG 迭代中的显式状态。
        std::vector<data_t> m_inv = build_jacobi_inverse(dataset);
        std::vector<data_t> x(dataset.x0().begin(), dataset.x0().end());
        std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> p(dataset.x0().begin(), dataset.x0().end());
        std::vector<data_t> spmv_out(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> metrics(2, 0.0);
        std::vector<data_t> dot_out(1, 0.0);
        std::vector<IterationTrace> traces;
        KernelTimingStats kernel_timing;

        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        std::cout << "[xplus-xrt] dataset=" << options.dataset_dir << "\n";
        std::cout << "[init] n=" << dataset.n()
                  << " nnz=" << dataset.nnz()
                  << " tau=" << std::scientific << std::setprecision(12) << config.tau
                  << " max_iters=" << std::defaultfloat << effective_max_iters
                  << " dtype=double\n";

        // XRT 流程从这里开始：
        // 1. 打开设备
        // 2. 下载 xclbin
        // 3. 根据名字创建 5 个独立 kernel 对象
        xrt::device device(options.device_index);
        auto uuid = device.load_xclbin(options.xclbin_path.string());

        auto spmv_kernel = xrt::kernel(device, uuid.get(), "spmv_csr_kernel");
        auto init_kernel = xrt::kernel(device, uuid.get(), "init_pcg_kernel");
        auto dot_kernel = xrt::kernel(device, uuid.get(), "dot_kernel");
        auto update_xrz_kernel = xrt::kernel(device, uuid.get(), "update_xrz_kernel");
        auto update_p_kernel = xrt::kernel(device, uuid.get(), "update_p_kernel");

        // 这一段把 CSR、向量和中间缓冲全部放进 device memory。
        // host 后续只在需要的时候同步极少量标量或最终 x。
        const auto h2d_start = Clock::now();

        auto row_ptr_bo = make_input_bo(device, spmv_kernel, 0, dataset.row_ptr());
        auto col_idx_bo = make_input_bo(device, spmv_kernel, 1, dataset.col_idx());
        auto values_bo = make_input_bo(device, spmv_kernel, 2, dataset.values());
        auto b_bo = make_input_bo(device, init_kernel, 0, dataset.b());
        auto x_bo = make_inout_bo(device, update_xrz_kernel, 0, x);
        auto r_bo = make_inout_bo(device, init_kernel, 3, r);
        auto z_bo = make_inout_bo(device, init_kernel, 4, z);
        auto p_bo = make_inout_bo(device, init_kernel, 5, p);
        auto spmv_out_bo = make_inout_bo(device, spmv_kernel, 4, spmv_out);
        auto m_inv_bo = make_input_bo(device, init_kernel, 2, m_inv);
        auto metrics_bo = make_inout_bo(device, init_kernel, 6, metrics);
        auto dot_out_bo = make_inout_bo(device, dot_kernel, 2, dot_out);
        const auto host_setup_end = Clock::now();
        const auto h2d_end = host_setup_end;
        auto kernel_ms = 0.0;

        // 初始化阶段的硬件执行顺序：
        //   spmv(x0 -> ax)
        //   init(ax,b,m_inv -> r,z,p,rz,rr)
        const auto kernel_start = Clock::now();
        auto run_begin = Clock::now();
        xrt::run init_spmv_run(spmv_kernel);
        init_spmv_run.set_arg(0, row_ptr_bo);
        init_spmv_run.set_arg(1, col_idx_bo);
        init_spmv_run.set_arg(2, values_bo);
        init_spmv_run.set_arg(3, p_bo);
        init_spmv_run.set_arg(4, spmv_out_bo);
        init_spmv_run.set_arg(5, dataset.n());
        init_spmv_run.start();
        init_spmv_run.wait();
        auto run_end = Clock::now();
        kernel_timing.spmv_total_ms += elapsed_ms(run_begin, run_end);
        kernel_timing.spmv_calls += 1;

        run_begin = Clock::now();
        xrt::run init_run(init_kernel);
        init_run.set_arg(0, b_bo);
        init_run.set_arg(1, spmv_out_bo);
        init_run.set_arg(2, m_inv_bo);
        init_run.set_arg(3, r_bo);
        init_run.set_arg(4, z_bo);
        init_run.set_arg(5, p_bo);
        init_run.set_arg(6, metrics_bo);
        init_run.set_arg(7, dataset.n());
        init_run.start();
        init_run.wait();
        run_end = Clock::now();
        kernel_timing.init_total_ms += elapsed_ms(run_begin, run_end);
        kernel_timing.init_calls += 1;

        metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(data_t), 0);
        auto metrics_mapped = metrics_bo.map<data_t*>();
        data_t rz = metrics_mapped[0];
        data_t rr = metrics_mapped[1];
        int iterations = 0;
        bool breakdown = false;

        for (int iteration = 0; iteration < effective_max_iters && rr > config.tau; ++iteration) {
            // 每一轮迭代严格按 host orchestration 串行启动 4 个阶段：
            // 1. spmv(p -> ap)
            // 2. dot(p, ap -> pAp)
            // 3. update_xrz(alpha -> x,r,z,rz_new,rr)
            // 4. update_p(beta -> p)
            //
            // 其中 alpha / beta / 收敛判断都由 host 计算和控制。
            run_begin = Clock::now();
            xrt::run spmv_run(spmv_kernel);
            spmv_run.set_arg(0, row_ptr_bo);
            spmv_run.set_arg(1, col_idx_bo);
            spmv_run.set_arg(2, values_bo);
            spmv_run.set_arg(3, p_bo);
            spmv_run.set_arg(4, spmv_out_bo);
            spmv_run.set_arg(5, dataset.n());
            spmv_run.start();
            spmv_run.wait();
            run_end = Clock::now();
            kernel_timing.spmv_total_ms += elapsed_ms(run_begin, run_end);
            kernel_timing.spmv_calls += 1;

            run_begin = Clock::now();
            xrt::run dot_run(dot_kernel);
            dot_run.set_arg(0, p_bo);
            dot_run.set_arg(1, spmv_out_bo);
            dot_run.set_arg(2, dot_out_bo);
            dot_run.set_arg(3, dataset.n());
            dot_run.start();
            dot_run.wait();
            run_end = Clock::now();
            kernel_timing.dot_total_ms += elapsed_ms(run_begin, run_end);
            kernel_timing.dot_calls += 1;

            dot_out_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, dot_out.size() * sizeof(data_t), 0);
            auto dot_mapped = dot_out_bo.map<data_t*>();
            const data_t p_ap = dot_mapped[0];
            // p^T A p 太小意味着 CG/PCG breakdown，需要安全退出。
            if (!std::isfinite(p_ap) || std::fabs(p_ap) <= project_xplus::cgsolver::kBreakdownEps ||
                !std::isfinite(rz) || std::fabs(rz) <= project_xplus::cgsolver::kBreakdownEps) {
                breakdown = true;
                break;
            }

            

            const data_t alpha = rz / p_ap;

            run_begin = Clock::now();
            xrt::run update_xrz_run(update_xrz_kernel);
            update_xrz_run.set_arg(0, x_bo);
            update_xrz_run.set_arg(1, p_bo);
            update_xrz_run.set_arg(2, r_bo);
            update_xrz_run.set_arg(3, spmv_out_bo);
            update_xrz_run.set_arg(4, m_inv_bo);
            update_xrz_run.set_arg(5, z_bo);
            update_xrz_run.set_arg(6, metrics_bo);
            update_xrz_run.set_arg(7, alpha);
            update_xrz_run.set_arg(8, dataset.n());
            update_xrz_run.start();
            update_xrz_run.wait();
            run_end = Clock::now();
            kernel_timing.update_xrz_total_ms += elapsed_ms(run_begin, run_end);
            kernel_timing.update_xrz_calls += 1;

            metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(data_t), 0);
            const data_t rz_old = rz;
            const data_t rz_new = metrics_mapped[0];
            const data_t rr_new = metrics_mapped[1];
            // update_xrz 后 host 先拿到新的 rz / rr，再决定 beta 和是否继续。
            if (!std::isfinite(rz_new) || !std::isfinite(rr_new) ||
                std::fabs(rz_old) <= project_xplus::cgsolver::kBreakdownEps) {
                breakdown = true;
                break;
            }

            const data_t beta = rz_new / rz_old;
            if (!std::isfinite(beta)) {
                breakdown = true;
                break;
            }

            run_begin = Clock::now();
            xrt::run update_p_run(update_p_kernel);
            update_p_run.set_arg(0, z_bo);
            update_p_run.set_arg(1, p_bo);
            update_p_run.set_arg(2, beta);
            update_p_run.set_arg(3, dataset.n());
            update_p_run.start();
            update_p_run.wait();
            run_end = Clock::now();
            kernel_timing.update_p_total_ms += elapsed_ms(run_begin, run_end);
            kernel_timing.update_p_calls += 1;

            rz = rz_new;
            rr = rr_new;
            iterations = iteration + 1;
            traces.push_back(IterationTrace{
                iteration,
                alpha,
                beta,
                rz,
                rr,
                std::sqrt(std::max(rr, data_t{0.0})),
            });
            if (options.verbose) {
                std::cout << "[iter " << std::setw(3) << std::setfill('0') << iteration << "] "
                          << std::scientific << std::setprecision(12)
                          << "alpha=" << alpha
                          << " beta=" << beta
                          << " rz=" << rz
                          << " rr=" << rr
                          << " residual=" << std::sqrt(std::max(rr, data_t{0.0})) << "\n";
            }
        }
        const auto kernel_end = Clock::now();
        kernel_ms = elapsed_ms(kernel_start, kernel_end);

        // 所有迭代结束后才把最终 x 拉回 host，避免每轮搬完整向量。
        const auto d2h_start = Clock::now();
        x_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, x.size() * sizeof(data_t), 0);
        auto x_mapped = x_bo.map<data_t*>();
        for (std::size_t index = 0; index < x.size(); ++index) {
            x[index] = x_mapped[index];
        }
        const auto d2h_end = Clock::now();

        const auto verify_start = Clock::now();
        // 最终检查包括：
        // 1. FPGA 输出解的残差
        // 2. FPGA 与 CPU golden 的最大绝对误差
        const double residual_l2 = compute_residual_norm_local(dataset, x);
        const double rhs_norm = project_xplus::cgsolver::l2_norm(dataset.b());
        const double residual_rel =
            residual_l2 / std::max(rhs_norm, static_cast<double>(project_xplus::cgsolver::kBreakdownEps));

        double max_abs_diff = 0.0;
        for (std::size_t index = 0; index < x.size(); ++index) {
            max_abs_diff =
                std::max(max_abs_diff, std::abs(x[index] - golden.golden.solution[index]));
        }

        const bool converged = (!breakdown && rr <= config.tau);
        const bool pass = converged && max_abs_diff <= 1.0e-8;
        const char* status = breakdown ? "breakdown" : (converged ? "converged" : "max_iter");
        const auto verify_end = Clock::now();
        const auto total_end = Clock::now();

        const double total_ms = elapsed_ms(total_start, total_end);
        const double host_setup_ms = elapsed_ms(host_setup_start, host_setup_end);
        const double h2d_ms = elapsed_ms(h2d_start, h2d_end);
        const double d2h_ms = elapsed_ms(d2h_start, d2h_end);
        const double verify_ms = elapsed_ms(verify_start, verify_end);

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << iterations
                  << " residual_abs=" << residual_l2
                  << " residual_rel=" << residual_rel
                  << " status=" << status << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden.summary.residual_l2
                  << " fpga_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff << "\n";

        if (options.timing) {
            std::cout << std::fixed << std::setprecision(3);
            // 第一层看 host 视角的端到端耗时。
            std::cout << "[host-timing-ms] total=" << total_ms
                      << " host_setup=" << host_setup_ms
                      << " buffer_h2d=" << h2d_ms
                      << " kernel_total=" << kernel_ms
                      << " buffer_d2h=" << d2h_ms
                      << " verify=" << verify_ms << "\n";
            // 第二层只看各 kernel 类别内部的累计/平均时间。
            std::cout << "[kernel-timing-ms] spmv_total=" << kernel_timing.spmv_total_ms
                      << " spmv_avg=" << (kernel_timing.spmv_calls > 0 ? kernel_timing.spmv_total_ms / kernel_timing.spmv_calls : 0.0)
                      << " init_total=" << kernel_timing.init_total_ms
                      << " dot_total=" << kernel_timing.dot_total_ms
                      << " dot_avg=" << (kernel_timing.dot_calls > 0 ? kernel_timing.dot_total_ms / kernel_timing.dot_calls : 0.0)
                      << " update_xrz_total=" << kernel_timing.update_xrz_total_ms
                      << " update_xrz_avg=" << (kernel_timing.update_xrz_calls > 0 ? kernel_timing.update_xrz_total_ms / kernel_timing.update_xrz_calls : 0.0)
                      << " update_p_total=" << kernel_timing.update_p_total_ms
                      << " update_p_avg=" << (kernel_timing.update_p_calls > 0 ? kernel_timing.update_p_total_ms / kernel_timing.update_p_calls : 0.0) << "\n";
        }

        if (!options.txt_out.empty()) {
            // txt 给人快速扫一眼；json 给后面的 HTML 报告生成器吃。
            write_text_report(options.txt_out,
                              options,
                              dataset,
                              effective_max_iters,
                              pass,
                              converged,
                              breakdown,
                              iterations,
                              rr,
                              residual_l2,
                              residual_rel,
                              max_abs_diff,
                              golden.summary.residual_l2,
                              total_ms,
                              host_setup_ms,
                              h2d_ms,
                              kernel_ms,
                              d2h_ms,
                              verify_ms,
                              kernel_timing);
        }

        if (!options.json_out.empty()) {
            write_json_report(options.json_out,
                              options,
                              dataset,
                              effective_max_iters,
                              pass,
                              converged,
                              breakdown,
                              iterations,
                              rr,
                              residual_l2,
                              residual_rel,
                              max_abs_diff,
                              golden.summary.residual_l2,
                              total_ms,
                              host_setup_ms,
                              h2d_ms,
                              kernel_ms,
                              d2h_ms,
                              verify_ms,
                              traces,
                              kernel_timing);
        }

        if (!converged) {
            return 2;
        }
        if (max_abs_diff > 1.0e-8) {
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
