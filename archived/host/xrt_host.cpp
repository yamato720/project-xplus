#include "cpu_reference.hpp"
#include "dataset_bridge.hpp"
#include "report_io.hpp"
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
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

namespace {

using project_xplus::cgsolver::data_t;
using project_xplus::cgsolver::CpuReferenceResult;
using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::IterationTrace;
using project_xplus::cgsolver::KernelTimingStats;
using project_xplus::cgsolver::SolverConfig;
using project_xplus::cgsolver::SpmvBlock;

struct WindowedBlockedSpmvMatrix {
    // 4x4 block/bitmap + column-window 分组后的 host 侧矩阵容器。
    //
    // row_ptr:
    //   window-major 的 block row 指针，长度为 num_windows * (num_block_rows + 1)。
    //   对第 w 个 x-window、第 br 个 block row：
    //     base = w * (num_block_rows + 1)
    //     row_ptr[base + br] ~ row_ptr[base + br + 1]
    //   是当前 window 和当前 block row 共同对应的非零块区间。
    //
    // col_idx:
    //   每个非零块所在的绝对 block column。虽然 blocks 已按 window 分组，
    //   kernel 仍需要 block column 还原真实列号并计算 x_window 内偏移。
    //
    // blocks:
    //   每个非零块的 16-bit bitmap 和紧凑 values。每个原始非零 4x4 block
    //   只会落入一个 x-window，因此不会因为 window 分组而复制 payload。
    std::vector<project_xplus::cgsolver::index_t> row_ptr;
    std::vector<project_xplus::cgsolver::index_t> col_idx;
    std::vector<SpmvBlock> blocks;
    int num_windows = 0;
    int num_block_rows = 0;
};

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
    // 创建只读输入 BO。
    //
    // BO(buffer object) 是 host 和 FPGA kernel 共享的一段 device memory。
    // kernel.group_id(arg_index) 会查询这个 kernel 参数在 xclbin/connectivity
    // 中绑定到哪个 memory group；在 U55C 配置里通常就是某个 HBM bank。
    //
    // 这里的生命周期是：
    //   1. host 分配 BO
    //   2. host 通过 map<T*>() 得到可写的 host 侧映射地址
    //   3. host 把 std::vector 内容 copy 到映射地址
    //   4. sync(TO_DEVICE) 把内容同步到 device memory
    //   5. kernel 运行期间只读这个 BO
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
xrt::bo make_inout_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, std::vector<T>& data) {
    // 创建读写 BO。
    //
    // 这类 BO 会先由 host 写入初始值，再由 kernel 在 device memory 上
    // 原地更新。kernel 结束后，host 必须显式 sync(FROM_DEVICE) 才能从
    // map<T*>() 的地址读到 device 侧最新结果。
    //
    // 典型例子：
    //   x_bo: host 写入 x0，kernel 原地更新成最终 x
    //   r/z/p/ap: host 写入 0，kernel 用作 PCG 中间向量
    //   metrics/status: host 写入 0，kernel 写回最终标量摘要和状态码
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

WindowedBlockedSpmvMatrix build_windowed_blocked_spmv_matrix(const Dataset& dataset) {
    // 把输入 CSR 转成 pcg_control_kernel 消费的 4x4 block/bitmap + window 分组格式。
    //
    // block 行:
    //   br = row / 4
    // block 列:
    //   bc = col / 4
    // block 内位置:
    //   pos = local_row * 4 + local_col
    //
    // 每个 block 用 16-bit mask 标出哪些位置有非零值，实际 values
    // 只按 row-major 顺序紧凑保存 mask=1 的位置。
    //
    // 转换分两步：
    //   1. 先按 block row 构造普通 4x4 block/bitmap，保持每行 block column 升序
    //   2. 再按 x-window 重新排列成 window-major block 流：
    //        window -> block row -> blocks in that row/window
    //
    // 这样 kernel 处理第 w 个 x-window 时，可以直接顺序读 A[:, W_w] 对应的
    // block 子流，不再每个 window 都在原始 block row 里查找/过滤。
    constexpr int kBlockSize = project_xplus::cgsolver::kSpmvBlockSize;
    constexpr int kBlockEntries = project_xplus::cgsolver::kSpmvBlockEntries;
    constexpr int kWindowSize = project_xplus::cgsolver::kSpmvWindowSize;

    WindowedBlockedSpmvMatrix blocked;
    const int n = dataset.n();
    const int num_block_rows = (n + kBlockSize - 1) / kBlockSize;
    const int num_windows = (n + kWindowSize - 1) / kWindowSize;
    blocked.num_windows = num_windows;
    blocked.num_block_rows = num_block_rows;

    std::vector<std::vector<std::pair<int, SpmvBlock>>> rows(static_cast<std::size_t>(num_block_rows));

    for (int block_row = 0; block_row < num_block_rows; ++block_row) {
        // 这里用 map 保证同一个 block row 内的 block column 按升序输出。
        // 对数值正确性不是必须，但有利于输出稳定和后续报告/调试。
        std::map<int, std::array<data_t, kBlockEntries>> block_values;

        for (int local_row = 0; local_row < kBlockSize; ++local_row) {
            const int row = block_row * kBlockSize + local_row;
            if (row >= n) {
                break;
            }

            for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
                 offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
                 ++offset) {
                const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
                const int block_col = col / kBlockSize;
                const int local_col = col % kBlockSize;
                const int pos = local_row * kBlockSize + local_col;
                // 如果 CSR 里同一个 block 内同一位置有重复项，这里累加到
                // 同一个 block 位置，保证 block 格式仍表达数学上的同一个 A。
                block_values[block_col][static_cast<std::size_t>(pos)] +=
                    dataset.values()[static_cast<std::size_t>(offset)];
            }
        }

        for (const auto& [block_col, values] : block_values) {
            SpmvBlock block{};
            unsigned short mask = 0;
            int compact_index = 0;

            // 把 4x4 dense 临时块压成 bitmap + compact values。
            // mask 记录哪些位置非零；values 只保存这些非零位置的数值。
            for (int pos = 0; pos < kBlockEntries; ++pos) {
                const data_t value = values[static_cast<std::size_t>(pos)];
                if (value != 0.0) {
                    mask = static_cast<unsigned short>(mask | static_cast<unsigned short>(1u << pos));
                    block.values[compact_index++] = value;
                }
            }

            if (mask == 0) {
                continue;
            }

            // 当前 kernel 约定 mask 放在 indices[2] 和 indices[3]。
            // 低 8 位在 indices[2]，高 8 位在 indices[3]。
            block.indices[2] = static_cast<unsigned char>(mask & 0xffu);
            block.indices[3] = static_cast<unsigned char>((mask >> 8) & 0xffu);
            rows[static_cast<std::size_t>(block_row)].push_back(std::make_pair(block_col, block));
        }
    }

    blocked.row_ptr.reserve(static_cast<std::size_t>(num_windows) *
                            static_cast<std::size_t>(num_block_rows + 1));
    std::size_t total_blocks = 0;
    for (const auto& row_blocks : rows) {
        total_blocks += row_blocks.size();
    }
    blocked.col_idx.reserve(total_blocks);
    blocked.blocks.reserve(total_blocks);

    for (int window = 0; window < num_windows; ++window) {
        const int window_begin = window * kWindowSize;
        const int window_end = std::min(window_begin + kWindowSize, n);
        const int window_block_begin = window_begin / kBlockSize;
        const int window_block_end = (window_end + kBlockSize - 1) / kBlockSize;

        for (int block_row = 0; block_row < num_block_rows; ++block_row) {
            // 当前 window / block row 的起点。下一项会在本 row 结束后写入。
            blocked.row_ptr.push_back(
                static_cast<project_xplus::cgsolver::index_t>(blocked.blocks.size()));

            const auto& row_blocks = rows[static_cast<std::size_t>(block_row)];
            for (const auto& [block_col, block] : row_blocks) {
                if (block_col >= window_block_begin && block_col < window_block_end) {
                    blocked.col_idx.push_back(block_col);
                    blocked.blocks.push_back(block);
                }
            }
        }

        // 当前 window 最后一个 row_ptr 入口。这样每个 window 都有完整的
        // num_block_rows + 1 个 row pointer，kernel 只需按 window_id 取 base。
        blocked.row_ptr.push_back(
            static_cast<project_xplus::cgsolver::index_t>(blocked.blocks.size()));
    }

    return blocked;
}

double compute_residual_norm_local(const Dataset& dataset, const std::vector<data_t>& x) {
    // 最终残差用 CPU 侧再算一遍，避免把验证逻辑混进 FPGA kernel。
    std::vector<double> x_as_double(x.begin(), x.end());
    return project_xplus::cgsolver::compute_residual_norm(dataset, x_as_double);
}

std::vector<IterationTrace> build_report_traces(const CpuReferenceResult& golden) {
    // 当前 XRT 默认路径只 launch 一次 pcg_control_kernel。
    // kernel 为了保持接口轻量，只回写最终 metrics/status，不回写每轮 alpha/beta/rr。
    // 因此报告里的迭代曲线来自 CPU reference 的同一套 Jacobi-PCG 递推，
    // 用来恢复“每轮收敛趋势”可视化；它不是硬件逐轮 trace。
    std::vector<IterationTrace> traces;
    traces.reserve(golden.golden.iteration_trace.size());
    for (const auto& item : golden.golden.iteration_trace) {
        traces.push_back(IterationTrace{
            item.iteration,
            item.alpha,
            item.beta,
            item.rz,
            item.rr,
            item.residual,
        });
    }
    return traces;
}

const char* kernel_status_name(const int status_code) {
    switch (status_code) {
        case 0:
            return "converged";
        case 1:
            return "max_iter";
        case 2:
            return "breakdown";
        default:
            return "breakdown";
    }
}

}

int main(int argc, char** argv) {
    try {
        const auto total_start = Clock::now();
        const HostOptions options = parse_args(argc, argv);
        const auto host_setup_start = Clock::now();
        const Dataset dataset = Dataset::load(options.dataset_dir);

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;
        // 先保留一份 CPU golden，用于最终结果与残差对照。
        const auto golden = project_xplus::cgsolver::run_cpu_reference(dataset, config);

        // 这些向量和标量都对应 Jacobi-PCG 迭代中的显式状态。
        std::vector<data_t> m_inv = build_jacobi_inverse(dataset);
        // 当前硬件 kernel 消费按 x-window 分组的 4x4 block/bitmap SpMV 格式；
        // 原始数据集仍然是 CSR。这个转换只做一次，因为 PCG 全流程中矩阵 A 不变。
        const WindowedBlockedSpmvMatrix blocked_matrix = build_windowed_blocked_spmv_matrix(dataset);
        std::vector<data_t> x(dataset.x0().begin(), dataset.x0().end());
        std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> ap(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<data_t> metrics(4, 0.0);
        std::vector<int> kernel_status(2, 0);
        std::vector<IterationTrace> traces = build_report_traces(golden);
        KernelTimingStats kernel_timing;

        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        std::cout << "[xplus-xrt] dataset=" << options.dataset_dir << "\n";
        std::cout << "[init] n=" << dataset.n()
                  << " nnz=" << dataset.nnz()
                  << " spmv_blocks=" << blocked_matrix.blocks.size()
                  << " spmv_windows=" << blocked_matrix.num_windows
                  << " tau=" << std::scientific << std::setprecision(12) << config.tau
                  << " max_iters=" << std::defaultfloat << effective_max_iters
                  << " dtype=double\n";

        // XRT 流程从这里开始：
        // 1. 打开设备
        // 2. 下载 xclbin
        // 3. 创建单顶层 PCG 控制 kernel
        xrt::device device(options.device_index);
        auto uuid = device.load_xclbin(options.xclbin_path.string());

        auto pcg_kernel = xrt::kernel(device, uuid.get(), "pcg_control_kernel");

        // 这一段把 block/bitmap SpMV 矩阵和 PCG 状态向量都放进 device memory。
        // x/r/z/p/ap 常驻 HBM，由 pcg_control_kernel 原地更新；host 后续只
        // 同步少量标量和最终 x。
        const auto h2d_start = Clock::now();

        // 下面每个 BO 都对应 pcg_control_kernel 的一个 m_axi 端口。
        // arg_index 必须和 kernels/pcg_control_kernel.cpp 的函数参数顺序一致；
        // connectivity_u55c.cfg 再根据端口名把这些 BO 放到对应 HBM bank。
        //
        // 只读矩阵 BO。它们从 host 侧 windowed block/bitmap 矩阵生成，
        // kernel 全程只读复用，不会在 PCG 迭代中改变。
        //
        // a_win_row_ptr_bo:
        //   kernel arg 0 -> a_win_row_ptr
        //   window-major block row 指针。对第 w 个 x-window、第 br 个 block row：
        //     base = w * (num_block_rows + 1)
        //     a_win_row_ptr[base + br] 到 a_win_row_ptr[base + br + 1]
        //   给出当前 window/row 的非零 block 区间。注意它不是原始 CSR row_ptr。
        auto a_win_row_ptr_bo = make_input_bo(device, pcg_kernel, 0, blocked_matrix.row_ptr);
        // a_win_col_idx_bo:
        //   kernel arg 1 -> a_win_col_idx
        //   每个 windowed 非零 4x4 block 的绝对 block column 索引。真正列号
        //   需要在 kernel 内用 block_col * kSpmvBlockSize + local_col 还原。
        auto a_win_col_idx_bo = make_input_bo(device, pcg_kernel, 1, blocked_matrix.col_idx);
        // a_win_blocks_bo:
        //   kernel arg 2 -> a_win_blocks
        //   每个非零 block 的 payload。SpmvBlock 里 values[] 保存 compact
        //   非零值，indices[2:3] 保存 16-bit occupancy mask。每个 block 只落在
        //   一个 x-window 中，因此 window 分组不会复制 block payload。
        auto a_win_blocks_bo = make_input_bo(device, pcg_kernel, 2, blocked_matrix.blocks);

        // 只读向量 BO。
        //
        // b_bo:
        //   kernel arg 3 -> b
        //   线性方程 A*x=b 的右端向量。kernel 只在初始化残差 r0=b-A*x0
        //   时读取，之后不改写。
        auto b_bo = make_input_bo(device, pcg_kernel, 3, dataset.b());
        // m_inv_bo:
        //   kernel arg 4 -> m_inv
        //   Jacobi 预条件器对角逆，m_inv[i] = 1 / A[i,i]。kernel 每轮用它
        //   计算 z = M^{-1}r，整个求解过程中只读。
        auto m_inv_bo = make_input_bo(device, pcg_kernel, 4, m_inv);

        // 读写向量 BO。这几条向量常驻 HBM，避免每轮 PCG 在 host/device
        // 之间搬运完整向量。kernel 在一次 launch 内原地更新它们。
        //
        // x_bo:
        //   kernel arg 5 -> x
        //   输入时是初始解 x0，kernel 每轮做 x = x + alpha*p，结束后
        //   host 只回读这个 BO 得到最终解。
        auto x_bo = make_inout_bo(device, pcg_kernel, 5, x);
        // r_bo:
        //   kernel arg 6 -> r
        //   残差向量。host 初始化为 0，kernel 先写 r0=b-A*x0，之后每轮
        //   原地更新 r = r - alpha*ap。
        auto r_bo = make_inout_bo(device, pcg_kernel, 6, r);
        // z_bo:
        //   kernel arg 7 -> z
        //   预条件残差向量。host 初始化为 0，kernel 写 z=M^{-1}r，并用
        //   它参与 rz = r^T z 和 p 更新。
        auto z_bo = make_inout_bo(device, pcg_kernel, 7, z);
        // p_bo:
        //   kernel arg 8 -> p
        //   PCG 搜索方向。kernel 初始化 p0=z0，之后每轮做
        //   p = z + beta*p。
        auto p_bo = make_inout_bo(device, pcg_kernel, 8, p);
        // ap_bo:
        //   kernel arg 9 -> ap
        //   SpMV 输出复用缓冲。初始化阶段临时表示 A*x0；主循环阶段表示
        //   A*p。host 不需要回读它，只用于 kernel 内计算 pAp 和更新 r。
        auto ap_bo = make_inout_bo(device, pcg_kernel, 9, ap);

        // 读写标量输出 BO。
        //
        // metrics_bo:
        //   kernel arg 10 -> metrics
        //   长度为 4，kernel 结束后写回 [rz, rr, pAp, alpha]，供 host
        //   打印摘要、写报告和判断 residual 相关信息。
        auto metrics_bo = make_inout_bo(device, pcg_kernel, 10, metrics);
        // status_bo:
        //   kernel arg 11 -> status
        //   长度为 2，kernel 结束后写回 [status_code, iterations]。
        //   status_code: 0 converged, 1 max_iter, 2 breakdown。
        auto status_bo = make_inout_bo(device, pcg_kernel, 11, kernel_status);
        const auto host_setup_end = Clock::now();
        const auto h2d_end = host_setup_end;
        auto kernel_ms = 0.0;

        // 单 kernel 版本把完整 Jacobi-PCG 控制流放进 FPGA：
        //   init spmv/init -> loop(spmv/dot/alpha/update_xrz/beta/update_p)
        // kernel 会把收敛、max_iter 和 breakdown 状态写回 status_bo。
        const auto kernel_start = Clock::now();
        auto run_begin = Clock::now();
        xrt::run pcg_run(pcg_kernel);
        // 把 BO 绑定到 kernel 参数。
        //
        // set_arg 这里只传 BO 句柄，不再复制数据；真实的数据已经在前面
        // sync(TO_DEVICE) 时进入 device memory。kernel 运行时按 arg_index
        // 访问对应 m_axi 端口。
        //
        // 参数 0~2 是按 window 分组的分块 SpMV 矩阵：
        //   a_win_row_ptr / a_win_col_idx / a_win_blocks。
        // 参数 3~4 是只读向量：b / m_inv。
        // 参数 5~9 是 HBM 常驻 PCG 向量：x/r/z/p/ap。
        // 参数 10~11 是 kernel 写回给 host 的标量摘要和状态。
        pcg_run.set_arg(0, a_win_row_ptr_bo);
        pcg_run.set_arg(1, a_win_col_idx_bo);
        pcg_run.set_arg(2, a_win_blocks_bo);
        pcg_run.set_arg(3, b_bo);
        pcg_run.set_arg(4, m_inv_bo);
        pcg_run.set_arg(5, x_bo);
        pcg_run.set_arg(6, r_bo);
        pcg_run.set_arg(7, z_bo);
        pcg_run.set_arg(8, p_bo);
        pcg_run.set_arg(9, ap_bo);
        pcg_run.set_arg(10, metrics_bo);
        pcg_run.set_arg(11, status_bo);
        pcg_run.set_arg(12, config.tau);
        pcg_run.set_arg(13, effective_max_iters);
        pcg_run.set_arg(14, dataset.n());
        pcg_run.start();
        pcg_run.wait();
        auto run_end = Clock::now();
        kernel_timing.pcg_control_total_ms += elapsed_ms(run_begin, run_end);
        kernel_timing.pcg_control_calls += 1;

        // kernel 结束后，metrics/status 的最新值只在 device memory 里。
        // 必须先 sync(FROM_DEVICE)，host 侧 map 指针读到的才是 kernel
        // 写回后的 [rz, rr, pAp, alpha] 和 [status_code, iterations]。
        metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(data_t), 0);
        status_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, kernel_status.size() * sizeof(int), 0);
        auto metrics_mapped = metrics_bo.map<data_t*>();
        auto status_mapped = status_bo.map<int*>();
        const data_t rz = metrics_mapped[0];
        const data_t rr = metrics_mapped[1];
        const int status_code = status_mapped[0];
        const int iterations = status_mapped[1];
        const bool breakdown = (status_code == 2 || status_code < 0 || status_code > 2);
        if (options.verbose) {
            std::cout << "[kernel-summary] status=" << kernel_status_name(status_code)
                      << " iterations=" << iterations
                      << " rz=" << std::scientific << std::setprecision(12) << rz
                      << " rr=" << rr
                      << " last_pAp=" << metrics_mapped[2]
                      << " last_alpha=" << metrics_mapped[3] << "\n";
        }
        const auto kernel_end = Clock::now();
        kernel_ms = elapsed_ms(kernel_start, kernel_end);

        // 所有迭代结束后才把最终 x 拉回 host，避免每轮搬完整向量。
        // r/z/p/ap 都只是 kernel 内部状态，本次 host 校验只需要最终 x，
        // 所以不回读这些中间 BO。
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

        const bool converged = (status_code == 0);
        const bool pass = converged && max_abs_diff <= 1.0e-8;
        const char* status = kernel_status_name(status_code);
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
            std::cout << "[kernel-timing-ms] pcg_control_total=" << kernel_timing.pcg_control_total_ms
                      << " pcg_control_avg=" << (kernel_timing.pcg_control_calls > 0 ? kernel_timing.pcg_control_total_ms / kernel_timing.pcg_control_calls : 0.0)
                      << " spmv_total=" << kernel_timing.spmv_total_ms
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
