#ifndef PROJECT_XPLUS_REPORT_IO_HPP
#define PROJECT_XPLUS_REPORT_IO_HPP

#include "dataset_bridge.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace project_xplus::cgsolver {

struct IterationTrace {
    int iteration = 0;
    double alpha = 0.0;
    double beta = 0.0;
    double rz = 0.0;
    double rr = 0.0;
    double residual = 0.0;
};

struct KernelTimingStats {
    // 这些计时按 kernel 类别累计，方便和 host 总时长拆开看。
    double pcg_control_total_ms = 0.0;
    double spmv_total_ms = 0.0;
    double init_total_ms = 0.0;
    double dot_total_ms = 0.0;
    double update_xrz_total_ms = 0.0;
    double update_p_total_ms = 0.0;
    int pcg_control_calls = 0;
    int spmv_calls = 0;
    int init_calls = 0;
    int dot_calls = 0;
    int update_xrz_calls = 0;
    int update_p_calls = 0;
};

inline bool has_split_kernel_timing(const KernelTimingStats& kernel_timing) {
    return kernel_timing.spmv_calls > 0 || kernel_timing.init_calls > 0 ||
           kernel_timing.dot_calls > 0 || kernel_timing.update_xrz_calls > 0 ||
           kernel_timing.update_p_calls > 0;
}

template <typename HostOptionsT>
inline std::string json_escape(const std::string& input) {
    (void)sizeof(HostOptionsT);
    std::string output;
    output.reserve(input.size() + 8);
    for (const char ch : input) {
        switch (ch) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += ch;
                break;
        }
    }
    return output;
}

inline void ensure_parent_dir(const std::filesystem::path& path) {
    if (!path.empty() && path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
}

template <typename HostOptionsT>
inline void write_text_report(const std::filesystem::path& path,
                              const HostOptionsT& options,
                              const Dataset& dataset,
                              const int effective_max_iters,
                              const bool pass,
                              const bool converged,
                              const bool breakdown,
                              const int iterations,
                              const double final_rr,
                              const double residual_l2,
                              const double residual_rel,
                              const double max_abs_diff,
                              const double golden_residual_l2,
                              const double total_ms,
                              const double host_setup_ms,
                              const double h2d_ms,
                              const double kernel_ms,
                              const double d2h_ms,
                              const double verify_ms,
                              const KernelTimingStats& kernel_timing) {
    ensure_parent_dir(path);
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open text report: " + path.string());
    }

    output << "[Problem]\n";
    output << "dataset            : " << options.dataset_dir << "\n";
    output << "n / nnz            : " << dataset.n() << " / " << dataset.nnz() << "\n";
    output << "max_iters          : " << effective_max_iters << "\n";
    output << std::scientific << std::setprecision(12);
    output << "tau                : " << options.tau << "\n";
    output << "device_index       : " << options.device_index << "\n\n";

    output << "[Accuracy]\n";
    output << "pass               : " << (pass ? "yes" : "no") << "\n";
    output << "converged          : " << (converged ? "yes" : "no") << "\n";
    output << "status             : " << (breakdown ? "breakdown" : (converged ? "converged" : "max_iter")) << "\n";
    output << "iterations         : " << iterations << "\n";
    output << "final_rr           : " << final_rr << "\n";
    output << "final_residual     : " << residual_l2 << "\n";
    output << "residual_rel       : " << residual_rel << "\n";
    output << "max_abs_diff       : " << max_abs_diff << "\n";
    output << "golden_residual    : " << golden_residual_l2 << "\n\n";

    output << "[Host Timing ms]\n";
    output << std::fixed << std::setprecision(3);
    output << "total              : " << total_ms << "\n";
    output << "host_setup         : " << host_setup_ms << "\n";
    output << "buffer_h2d         : " << h2d_ms << "\n";
    output << "kernel_total       : " << kernel_ms << "\n";
    output << "buffer_d2h         : " << d2h_ms << "\n";
    output << "verify             : " << verify_ms << "\n";

    output << "\n[Kernel Timing ms]\n";
    output << "pcg_control_total  : " << kernel_timing.pcg_control_total_ms << "\n";
    output << "pcg_control_avg    : "
           << (kernel_timing.pcg_control_calls > 0 ? kernel_timing.pcg_control_total_ms / kernel_timing.pcg_control_calls : 0.0) << "\n";
    output << "split_timing_note  : "
           << (has_split_kernel_timing(kernel_timing)
                   ? "split kernel timings are available"
                   : "not available for single pcg_control_kernel runs") << "\n";
    output << "spmv_total         : " << kernel_timing.spmv_total_ms << "\n";
    output << "spmv_avg           : "
           << (kernel_timing.spmv_calls > 0 ? kernel_timing.spmv_total_ms / kernel_timing.spmv_calls : 0.0) << "\n";
    output << "init_total         : " << kernel_timing.init_total_ms << "\n";
    output << "init_avg           : "
           << (kernel_timing.init_calls > 0 ? kernel_timing.init_total_ms / kernel_timing.init_calls : 0.0) << "\n";
    output << "dot_total          : " << kernel_timing.dot_total_ms << "\n";
    output << "dot_avg            : "
           << (kernel_timing.dot_calls > 0 ? kernel_timing.dot_total_ms / kernel_timing.dot_calls : 0.0) << "\n";
    output << "update_xrz_total   : " << kernel_timing.update_xrz_total_ms << "\n";
    output << "update_xrz_avg     : "
           << (kernel_timing.update_xrz_calls > 0 ? kernel_timing.update_xrz_total_ms / kernel_timing.update_xrz_calls : 0.0) << "\n";
    output << "update_p_total     : " << kernel_timing.update_p_total_ms << "\n";
    output << "update_p_avg       : "
           << (kernel_timing.update_p_calls > 0 ? kernel_timing.update_p_total_ms / kernel_timing.update_p_calls : 0.0) << "\n";
}

template <typename HostOptionsT>
inline void write_json_report(const std::filesystem::path& path,
                              const HostOptionsT& options,
                              const Dataset& dataset,
                              const int effective_max_iters,
                              const bool pass,
                              const bool converged,
                              const bool breakdown,
                              const int iterations,
                              const double final_rr,
                              const double residual_l2,
                              const double residual_rel,
                              const double max_abs_diff,
                              const double golden_residual_l2,
                              const double total_ms,
                              const double host_setup_ms,
                              const double h2d_ms,
                              const double kernel_ms,
                              const double d2h_ms,
                              const double verify_ms,
                              const std::vector<IterationTrace>& traces,
                              const KernelTimingStats& kernel_timing) {
    ensure_parent_dir(path);
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open json report: " + path.string());
    }

    output << std::scientific << std::setprecision(12);
    output << "{\n";
    output << "  \"dataset\": {\n";
    output << "    \"path\": \"" << json_escape<HostOptionsT>(options.dataset_dir.string()) << "\",\n";
    output << "    \"xclbin\": \"" << json_escape<HostOptionsT>(options.xclbin_path.string()) << "\",\n";
    output << "    \"n\": " << dataset.n() << ",\n";
    output << "    \"nnz\": " << dataset.nnz() << ",\n";
    output << "    \"max_iters\": " << effective_max_iters << ",\n";
    output << "    \"tau\": " << options.tau << ",\n";
    output << "    \"device_index\": " << options.device_index << "\n";
    output << "  },\n";
    output << "  \"result\": {\n";
    output << "    \"pass\": " << (pass ? "true" : "false") << ",\n";
    output << "    \"converged\": " << (converged ? "true" : "false") << ",\n";
    output << "    \"status\": \"" << (breakdown ? "breakdown" : (converged ? "converged" : "max_iter")) << "\",\n";
    output << "    \"iterations\": " << iterations << ",\n";
    output << "    \"final_rr\": " << final_rr << ",\n";
    output << "    \"final_residual_norm\": " << residual_l2 << ",\n";
    output << "    \"residual_rel\": " << residual_rel << ",\n";
    output << "    \"solution_max_abs_diff\": " << max_abs_diff << ",\n";
    output << "    \"golden_residual_norm\": " << golden_residual_l2 << "\n";
    output << "  },\n";
    output << "  \"host_timing_ms\": {\n";
    output << std::fixed << std::setprecision(3);
    output << "    \"total\": " << total_ms << ",\n";
    output << "    \"host_setup\": " << host_setup_ms << ",\n";
    output << "    \"buffer_h2d\": " << h2d_ms << ",\n";
    output << "    \"kernel_total\": " << kernel_ms << ",\n";
    output << "    \"buffer_d2h\": " << d2h_ms << ",\n";
    output << "    \"verify\": " << verify_ms << "\n";
    output << "  },\n";
    output << "  \"kernel_timing_ms\": {\n";
    output << "    \"pcg_control_total\": " << kernel_timing.pcg_control_total_ms << ",\n";
    output << "    \"pcg_control_avg\": "
           << (kernel_timing.pcg_control_calls > 0 ? kernel_timing.pcg_control_total_ms / kernel_timing.pcg_control_calls : 0.0) << ",\n";
    output << "    \"pcg_control_calls\": " << kernel_timing.pcg_control_calls << ",\n";
    output << "    \"spmv_total\": " << kernel_timing.spmv_total_ms << ",\n";
    output << "    \"spmv_avg\": "
           << (kernel_timing.spmv_calls > 0 ? kernel_timing.spmv_total_ms / kernel_timing.spmv_calls : 0.0) << ",\n";
    output << "    \"spmv_calls\": " << kernel_timing.spmv_calls << ",\n";
    output << "    \"init_total\": " << kernel_timing.init_total_ms << ",\n";
    output << "    \"init_avg\": "
           << (kernel_timing.init_calls > 0 ? kernel_timing.init_total_ms / kernel_timing.init_calls : 0.0) << ",\n";
    output << "    \"init_calls\": " << kernel_timing.init_calls << ",\n";
    output << "    \"dot_total\": " << kernel_timing.dot_total_ms << ",\n";
    output << "    \"dot_avg\": "
           << (kernel_timing.dot_calls > 0 ? kernel_timing.dot_total_ms / kernel_timing.dot_calls : 0.0) << ",\n";
    output << "    \"dot_calls\": " << kernel_timing.dot_calls << ",\n";
    output << "    \"update_xrz_total\": " << kernel_timing.update_xrz_total_ms << ",\n";
    output << "    \"update_xrz_avg\": "
           << (kernel_timing.update_xrz_calls > 0 ? kernel_timing.update_xrz_total_ms / kernel_timing.update_xrz_calls : 0.0) << ",\n";
    output << "    \"update_xrz_calls\": " << kernel_timing.update_xrz_calls << ",\n";
    output << "    \"update_p_total\": " << kernel_timing.update_p_total_ms << ",\n";
    output << "    \"update_p_avg\": "
           << (kernel_timing.update_p_calls > 0 ? kernel_timing.update_p_total_ms / kernel_timing.update_p_calls : 0.0) << ",\n";
    output << "    \"update_p_calls\": " << kernel_timing.update_p_calls << ",\n";
    output << "    \"split_timing_available\": "
           << (has_split_kernel_timing(kernel_timing) ? "true" : "false") << ",\n";
    output << "    \"timing_model\": \""
           << (has_split_kernel_timing(kernel_timing) ? "split_kernels" : "single_control_kernel")
           << "\"\n";
    output << "  },\n";
    output << "  \"trace_metadata\": {\n";
    output << "    \"source\": \"" << (traces.empty() ? "none" : "cpu_reference") << "\",\n";
    output << "    \"note\": \""
           << (traces.empty()
                   ? "no per-iteration trace was collected"
                   : "single control-kernel hardware currently reports only final metrics; trace is generated by CPU reference using the same PCG recurrence")
           << "\"\n";
    output << "  },\n";
    output << "  \"iterations_trace\": [\n";
    output << std::scientific << std::setprecision(12);
    for (std::size_t index = 0; index < traces.size(); ++index) {
        const IterationTrace& trace = traces[index];
        output << "    {\n";
        output << "      \"iteration\": " << trace.iteration << ",\n";
        output << "      \"alpha\": " << trace.alpha << ",\n";
        output << "      \"beta\": " << trace.beta << ",\n";
        output << "      \"rz\": " << trace.rz << ",\n";
        output << "      \"rr\": " << trace.rr << ",\n";
        output << "      \"residual\": " << trace.residual << "\n";
        output << "    }" << (index + 1 == traces.size() ? "\n" : ",\n");
    }
    output << "  ]\n";
    output << "}\n";
}

}  // namespace project_xplus::cgsolver

#endif
