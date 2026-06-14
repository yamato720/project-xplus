#include <cmath>
#include <algorithm>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <bitset>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"
#include "Cuper_common.h"

using namespace std;

// TAPA mmap 的 host buffer 需要使用 aligned_allocator。
// 这个别名用于 SpElement_list_ptr、Matrix_data、X/Y 等要传给 kernel 的数组。
template <typename T>
using aligned_vector = std::vector<T, tapa::aligned_allocator<T> >;

namespace fs = std::filesystem;

int EnvInt(const char* name, const int default_value) {
    // 运行脚本通过环境变量传 MAX_ITERS 等小参数；未设置时使用 demo 默认值。
    const char* value = std::getenv(name);
    return value == nullptr ? default_value : std::atoi(value);
}

float EnvFloat(const char* name, const float default_value) {
    // Tau 等浮点配置走同一套环境变量读取，避免为了 demo 再扩命令行参数。
    const char* value = std::getenv(name);
    return value == nullptr ? default_value : std::atof(value);
}

double JacobiCyclesToMs(const double cycles, const double clock_period_ns) {
    return cycles * clock_period_ns * 1.0e-6;
}

static constexpr INDEX_TYPE kJacobiStatusSentinelBase = 0x51510000;
static constexpr INDEX_TYPE kJacobiDebugSentinelBase = 0x53530000;
static constexpr INDEX_TYPE kJacobiTraceDebugWords = 256;
static constexpr INDEX_TYPE kJacobiTracePerSourceBase = 64;
static constexpr INDEX_TYPE kJacobiTracePerSourceStride = 4;
static constexpr INDEX_TYPE kJacobiTraceSourceMax = 47;
static constexpr double kJacobiMetricsSentinelBase = -1000000.0;

bool IsJacobiDebugSentinel(const INDEX_TYPE index, const INDEX_TYPE value) {
    return value == kJacobiDebugSentinelBase + index;
}

#ifdef JACOBI_TRACE_ENABLED
const char* JacobiTracePhaseName(const INDEX_TYPE phase) {
    switch (phase) {
    case 1: return "enter_round";
    case 2: return "progress";
    case 3: return "wait";
    case 4: return "done_round";
    case 5: return "recv";
    case 6: return "send";
    case 7: return "read_issue";
    case 8: return "read_resp";
    case 9: return "write_issue";
    case 10: return "write_resp";
    case 11: return "feedback";
    case 12: return "frame";
    case 255: return "stop";
    default: return "unknown";
    }
}

void PrintJacobiTraceSourceLabel(const INDEX_TYPE source) {
    if (source == 1) {
        cout << "dispatcher";
    } else if (source == 2) {
        cout << "ptr_loader";
    } else if (source == 3) {
        cout << "vector_loader";
    } else if (source >= 4 && source < 20) {
        cout << "matrix_loader[" << (source - 4) << "]";
    } else if (source >= 20 && source < 36) {
        cout << "accumulator[" << (source - 20) << "]";
    } else if (source == 36) {
        cout << "frame_fork";
    } else if (source == 37) {
        cout << "coeff_loader";
    } else if (source >= 38 && source < 46) {
        cout << "pair_compute[" << (source - 38) << "]";
    } else if (source == 46) {
        cout << "pack_writer";
    } else if (source == 47) {
        cout << "x_hbm_writer";
    } else {
        cout << "source" << source;
    }
}

void PrintJacobiProbeSnapshot(const char* label,
                              const aligned_vector<INDEX_TYPE>& status_data,
                              const aligned_vector<double>& metrics_data) {
    const std::streamsize old_precision = cout.precision();
    const std::ios_base::fmtflags old_flags = cout.flags();
    cout << std::defaultfloat << std::setprecision(10);

    cout << "[" << label << "] Status[8..11]=";
    for (INDEX_TYPE index = 8; index < 12; ++index) {
        if (index != 8) {
            cout << ",";
        }
        cout << status_data[index];
    }
    cout << " Metrics[8..11]=";
    for (INDEX_TYPE index = 8; index < 12; ++index) {
        if (index != 8) {
            cout << ",";
        }
        cout << metrics_data[index];
    }
    cout << endl;

    cout.flags(old_flags);
    cout.precision(old_precision);
}
#endif

void PrintJacobiPrefinishSnapshot(const aligned_vector<INDEX_TYPE>& status_data,
                                  const aligned_vector<double>& metrics_data) {
    cout << "[jacobi-prefinish] Status[0..2]="
         << status_data[0] << ","
         << status_data[1] << ","
         << status_data[2]
         << " Metrics[0..7]=";
    for (INDEX_TYPE index = 0; index < 8; ++index) {
        if (index != 0) {
            cout << ",";
        }
        cout << metrics_data[index];
    }
    cout << endl;

#ifdef JACOBI_TRACE_ENABLED
    PrintJacobiProbeSnapshot("jacobi-prefinish-probe", status_data, metrics_data);
#endif
}

#ifdef JACOBI_TRACE_ENABLED
void PrintJacobiDebugBuffer(const aligned_vector<INDEX_TYPE>& debug_data) {
    const INDEX_TYPE packed_event = debug_data[2];
    const INDEX_TYPE source = (packed_event >> 24) & 0xff;
    const INDEX_TYPE phase = (packed_event >> 16) & 0xff;
    const INDEX_TYPE lane = packed_event & 0xffff;

    cout << "[jacobi-trace] heartbeat=" << debug_data[0]
         << " event_count=" << debug_data[1]
         << " last_source=";
    PrintJacobiTraceSourceLabel(source);
    cout << "(" << source << ")"
         << " last_phase=" << JacobiTracePhaseName(phase) << "(" << phase << ")"
         << " last_lane=" << lane
         << " last_value=" << debug_data[3]
         << " stop_marker=" << debug_data[15]
         << endl;

    cout << "[jacobi-trace-mmap] write_issue=" << debug_data[5]
         << " write_resp=" << debug_data[6]
         << " stop_seen=" << debug_data[7]
         << endl;

    cout << "[jacobi-trace-probe] Debug[48..51]="
         << debug_data[48] << ","
         << debug_data[49] << ","
         << debug_data[50] << ","
         << debug_data[51]
         << endl;

    cout << "[jacobi-trace-legacy-slots]";
    for (INDEX_TYPE index = 16; index < 64; ++index) {
        if (!IsJacobiDebugSentinel(index, debug_data[index]) && debug_data[index] != 0) {
            cout << " d" << index << "=" << debug_data[index];
        }
    }
    cout << endl;

    cout << "[jacobi-trace-sources]" << endl;
    for (INDEX_TYPE source_index = 1; source_index <= kJacobiTraceSourceMax; ++source_index) {
        const INDEX_TYPE base = kJacobiTracePerSourceBase +
                                source_index * kJacobiTracePerSourceStride;
        if (base + 3 >= static_cast<INDEX_TYPE>(debug_data.size())) {
            break;
        }
        const bool touched =
            !IsJacobiDebugSentinel(base, debug_data[base]) ||
            !IsJacobiDebugSentinel(base + 1, debug_data[base + 1]) ||
            !IsJacobiDebugSentinel(base + 2, debug_data[base + 2]) ||
            !IsJacobiDebugSentinel(base + 3, debug_data[base + 3]);
        if (!touched) {
            continue;
        }
        cout << "  src=";
        PrintJacobiTraceSourceLabel(source_index);
        cout << "(" << source_index << ")"
             << " phase=" << JacobiTracePhaseName(debug_data[base])
             << "(" << debug_data[base] << ")"
             << " lane=" << debug_data[base + 1]
             << " value=" << debug_data[base + 2]
             << " event=" << debug_data[base + 3]
             << endl;
    }
}
#endif

template <typename... Args>
int64_t InvokeCuperJacobiIterationWithPrefinishDump(
    const std::string& bitstream,
    const aligned_vector<INDEX_TYPE>& status_data,
    const aligned_vector<double>& metrics_data,
#ifdef JACOBI_TRACE_ENABLED
    const aligned_vector<INDEX_TYPE>& debug_data,
#endif
    Args&&... args) {
    if (bitstream.empty()) {
        return tapa::invoke(CuperJacobiIteration,
                            bitstream,
                            std::forward<Args>(args)...);
    }

    fpga::Instance instance(bitstream);

    tapa::internal::frt_sync_kernel_instance = &instance;
    std::signal(SIGINT, &tapa::internal::kill_frt_sync_kernel);

    using JacobiInvoker = tapa::internal::invoker<decltype((CuperJacobiIteration))>;
    JacobiInvoker::set_fpga_args(instance,
                                 CuperJacobiIteration,
                                 std::index_sequence_for<Args...>{},
                                 std::forward<Args>(args)...);

    cout << "[tapa-invoke] before WriteToDevice" << endl;
    instance.WriteToDevice();
    cout << "[tapa-invoke] after WriteToDevice before Exec" << endl;
    instance.Exec();
#ifdef JACOBI_TRACE_ENABLED
    const int sample_delay_ms = EnvInt("JACOBI_PREFINISH_SAMPLE_DELAY_MS", 250);
    if (sample_delay_ms > 0) {
        cout << "[tapa-invoke] trace sample delay before ReadFromDevice: "
             << sample_delay_ms << " ms" << endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_delay_ms));
    }
#endif
    cout << "[tapa-invoke] after Exec before ReadFromDevice" << endl;
    instance.ReadFromDevice();
    cout << "[tapa-invoke] after ReadFromDevice before Finish" << endl;

    // Finish() 当前可能不返回；ReadFromDevice 后先打印已同步回 host 的 BO 快照。
    PrintJacobiPrefinishSnapshot(status_data, metrics_data);
#ifdef JACOBI_TRACE_ENABLED
    PrintJacobiDebugBuffer(debug_data);
#endif

    instance.Finish();
    cout << "[tapa-invoke] after Finish" << endl;

    std::signal(SIGINT, SIG_DFL);
    tapa::internal::frt_sync_kernel_instance = nullptr;

    return instance.ComputeTimeNanoSeconds();
}

bool IsCsrDatasetDir(const fs::path& path) {
    // Project-XPlus CSR 数据目录的最小约定：三数组文本文件必须同时存在。
    // b.txt 是可选 RHS，不参与这里的目录判定。
    return fs::is_directory(path) &&
           fs::exists(path / "row_ptr.txt") &&
           fs::exists(path / "col_idx.txt") &&
           fs::exists(path / "values.txt");
}

template <typename T>
void ReadTextArray(const fs::path& path, vector<T>& output) {
    // 简单文本数组读取器，服务于 data/suitesparse/.../csr 这类目录。
    // 这里保持顺序读取，不做逗号或 Matrix Market 解析。
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open " + path.string());
    }

    output.clear();
    T value{};
    while (input >> value) {
        output.push_back(value);
    }
    if (output.empty()) {
        throw std::runtime_error("empty array file: " + path.string());
    }
}

INDEX_TYPE ReadMetaInt(const fs::path& meta_path, const std::string& key, const INDEX_TYPE fallback) {
    // meta.txt 采用 key=value 文本格式。缺文件或缺 key 时回退到 fallback，
    // 例如没有显式 n 时默认认为矩阵是 n=m 的方阵。
    std::ifstream input(meta_path);
    if (!input) {
        return fallback;
    }

    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return static_cast<INDEX_TYPE>(std::atoi(line.substr(prefix.size()).c_str()));
        }
    }
    return fallback;
}

void ReadCsrDatasetDir(const fs::path& dataset_dir,
                       INDEX_TYPE& m,
                       INDEX_TYPE& n,
                       INDEX_TYPE& nnzR,
                       INDEX_TYPE& isSymmetric,
                       vector<INDEX_TYPE>& RowPtr,
                       vector<INDEX_TYPE>& ColIdx,
                       vector<VALUE_TYPE>& Val) {
    // 从 Project-XPlus CSR 目录加载 row_ptr/col_idx/values。
    // values.txt 先按 double 读入，再转成当前 kernel 使用的 VALUE_TYPE(float)。
    vector<double> values_fp64;
    ReadTextArray(dataset_dir / "row_ptr.txt", RowPtr);
    ReadTextArray(dataset_dir / "col_idx.txt", ColIdx);
    ReadTextArray(dataset_dir / "values.txt", values_fp64);

    if (RowPtr.size() < 2) {
        throw std::runtime_error("row_ptr.txt must contain at least two entries");
    }
    if (ColIdx.size() != values_fp64.size()) {
        throw std::runtime_error("col_idx.txt and values.txt size mismatch");
    }
    if (RowPtr.front() != 0) {
        throw std::runtime_error("row_ptr[0] must be 0");
    }
    if (RowPtr.back() != static_cast<INDEX_TYPE>(ColIdx.size())) {
        throw std::runtime_error("row_ptr.back() must equal col_idx size");
    }

    m = static_cast<INDEX_TYPE>(RowPtr.size()) - 1;
    n = ReadMetaInt(dataset_dir / "meta.txt", "n", m);
    nnzR = static_cast<INDEX_TYPE>(ColIdx.size());
    // 这些 CSR 子数据集已经是普通 CSR，不在 host 入口再做对称矩阵展开。
    isSymmetric = 0;

    for (INDEX_TYPE row = 0; row < m; ++row) {
        if (RowPtr[row] > RowPtr[row + 1]) {
            throw std::runtime_error("row_ptr must be nondecreasing");
        }
    }
    for (INDEX_TYPE offset = 0; offset < nnzR; ++offset) {
        if (ColIdx[offset] < 0 || ColIdx[offset] >= n) {
            throw std::runtime_error("col_idx out of range in " + dataset_dir.string());
        }
    }

    Val.resize(values_fp64.size());
    for (std::size_t index = 0; index < values_fp64.size(); ++index) {
        Val[index] = static_cast<VALUE_TYPE>(values_fp64[index]);
    }
}

bool TryReadCsrVector(const fs::path& path, const INDEX_TYPE expected_size, vector<VALUE_TYPE>& output) {
    // 可选读取 b.txt。不存在时返回 false，后面会用 A*ones 自动构造 RHS。
    if (!fs::exists(path)) {
        return false;
    }

    vector<double> values_fp64;
    ReadTextArray(path, values_fp64);
    if (values_fp64.size() != static_cast<std::size_t>(expected_size)) {
        throw std::runtime_error(path.string() + " length does not match matrix dimension");
    }

    output.resize(values_fp64.size());
    for (std::size_t index = 0; index < values_fp64.size(); ++index) {
        output[index] = static_cast<VALUE_TYPE>(values_fp64[index]);
    }
    return true;
}

bool PrepareJacobiVectorsAndR(const INDEX_TYPE n,
                              const vector<INDEX_TYPE>& RowPtr,
                              const vector<INDEX_TYPE>& ColIdx,
                              const vector<VALUE_TYPE>& Val,
                              const bool has_input_b,
                              vector<INDEX_TYPE>& R_RowPtr,
                              vector<INDEX_TYPE>& R_ColIdx,
                              vector<VALUE_TYPE>& R_Val,
                              vector<VALUE_TYPE>& B,
                              vector<VALUE_TYPE>& Diag_inv,
                              vector<VALUE_TYPE>& X) {
    // 如果数据集没有 b.txt，就令真实解 x*=ones，并用 b=A*ones 构造 RHS。
    // 这样 CPU reference 和 FPGA kernel 可以直接比较数值输出。
    if (!has_input_b) {
        vector<VALUE_TYPE> ones(n, 1.0f);
        vector<VALUE_TYPE> zero(n, 0.0f);
        SpMV_CPU_CSR(n, n, static_cast<INDEX_TYPE>(Val.size()), RowPtr, ColIdx, Val, ones, zero, B);
    }

    // host 侧在原始 CSR 行列号还清楚时拆 A = D + R：
    //   - 对角项累加进 D，用 Diag_inv 保存 1 / D[i]；
    //   - 非对角项保留进 R，kernel 端读 x_old 时取负并计算 -R*x_old。
    R_RowPtr.assign(n + 1, 0);
    R_ColIdx.clear();
    R_Val.clear();
    R_ColIdx.reserve(Val.size());
    R_Val.reserve(Val.size());

    for (INDEX_TYPE row = 0; row < n; ++row) {
        // Jacobi 需要 D^{-1}，所以每行必须能找到非零对角项。
        // kernel 端只消费 Diag_inv，避免在 Cuper 输出更新侧再做除法。
        bool found_diag = false;
        VALUE_TYPE diag_value = 0.0f;
        R_RowPtr[row] = static_cast<INDEX_TYPE>(R_ColIdx.size());
        for (INDEX_TYPE offset = RowPtr[row]; offset < RowPtr[row + 1]; ++offset) {
            if (ColIdx[offset] == row) {
                diag_value += Val[offset];
                found_diag = true;
            } else {
                R_ColIdx.push_back(ColIdx[offset]);
                R_Val.push_back(Val[offset]);
            }
        }
        if (!found_diag || diag_value == 0.0f) {
            cerr << "[Jacobi] missing or zero diagonal at row " << row << endl;
            return false;
        }
        Diag_inv[row] = 1.0f / diag_value;
        X[row] = 0.0f;
    }
    R_RowPtr[n] = static_cast<INDEX_TYPE>(R_ColIdx.size());
    return true;
}

INDEX_TYPE RunJacobiCpu(const INDEX_TYPE n,
                        const INDEX_TYPE max_iters,
                        const VALUE_TYPE tau,
                        const vector<INDEX_TYPE>& R_RowPtr,
                        const vector<INDEX_TYPE>& R_ColIdx,
                        const vector<VALUE_TYPE>& R_Val,
                        const vector<VALUE_TYPE>& B,
                        const vector<VALUE_TYPE>& Diag_inv,
                        vector<VALUE_TYPE>& X_ref,
                        VALUE_TYPE& final_diff) {
    // Hardware debug runs fixed-count Jacobi iterations. Keep the CPU reference
    // on the same contract so verification does not depend on tau early-exit.
    (void)tau;
    // CPU reference 使用和 FPGA 相同的单 buffer 合约：
    // 每轮先根据完整旧 X 算出 x_next，再整体替换 X，避免变成 Gauss-Seidel。
    vector<VALUE_TYPE> x(n, 0.0f);
    vector<VALUE_TYPE> x_next(n, 0.0f);
    vector<VALUE_TYPE> rx(n, 0.0f);
    const vector<VALUE_TYPE> zero(n, 0.0f);
    final_diff = 0.0f;

    for (INDEX_TYPE iter = 0; iter < max_iters; ++iter) {
        // FPGA 侧把对角项从 SpMV 矩阵里移除，并通过输入取负得到 -R*x_old。
        // CPU reference 直接算 R*x_old 再执行 b-rx，数学口径相同，方便逐元素比较。
        SpMV_CPU_CSR(n, n, static_cast<INDEX_TYPE>(R_Val.size()), R_RowPtr, R_ColIdx, R_Val, x, zero, rx);

        VALUE_TYPE diff_max = 0.0f;
        for (INDEX_TYPE row = 0; row < n; ++row) {
            const VALUE_TYPE next =
                (B[row] - rx[row]) * Diag_inv[row];
            const VALUE_TYPE diff = std::fabs(next - x[row]);
            x_next[row] = next;
            if (diff > diff_max) {
                diff_max = diff;
            }
        }

        final_diff = diff_max;
        x.swap(x_next);
    }

    X_ref = x;
    return max_iters;
}

int main(int argc, char* argv[]) {

    // 这个 host 是 Cuper-jacobi-iteration 子项目的 Jacobi demo 入口。
    // 输入可以是 Matrix Market .mtx 文件，也可以是 Project-XPlus CSR 目录。
    // 它会同时运行 CPU Jacobi reference 和 TAPA software simulation/hardware kernel。
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <matrix_file_or_csr_dir>" << endl;
        return 1;
    }

    char *filename = argv[1];

    cout << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "The Matrix name: \t\t\t" << filename << endl;

    // TAPA 软件模拟可以不传 bitstream；上板或 sw_emu/hw_emu 通常通过
    // BITFILE 指定 xclbin 路径。
    std::string bitstream;
    if(const auto bitstream_ptr = getenv("BITFILE")) {
        bitstream = bitstream_ptr;
    } else {
        cout << "[Warning] BITFILE environment variable not set!" << endl;
    }

    const fs::path input_path(filename);
    if (!fs::exists(input_path)) {
        cerr << "[Jacobi] input path does not exist: " << input_path << endl;
        return 1;
    }
    const bool read_csr_dir = IsCsrDatasetDir(input_path);
    INDEX_TYPE m, n, nnzR, isSymmetric;

    // Matrix Market 读入路径：先读尺寸，再分配 CSR/COO/向量空间。
    // 这里的 VALUE_TYPE/INDEX_TYPE 来自 Cuper.h，当前分别是 float/int。
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read CSR dir: \t\t\t" << (read_csr_dir ? "ON" : "OFF") << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Size...";

    vector<INDEX_TYPE> RowPtr;
    vector<INDEX_TYPE> ColIdx;
    vector<VALUE_TYPE> Val;

    if (read_csr_dir) {
        try {
            ReadCsrDatasetDir(input_path, m, n, nnzR, isSymmetric, RowPtr, ColIdx, Val);
        } catch (const std::exception& error) {
            cerr << "[Read Matrix] " << error.what() << endl;
            return 1;
        }
    } else {
        Read_matrix_size(filename, &m, &n, &nnzR, &isSymmetric);
    }

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Matrix Size: \t\t\t" << m << " x " << n << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "NNZ: \t\t\t\t" << nnzR << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Allocate Memory Space...";

    if (!read_csr_dir) {
        RowPtr.resize(m + 1);
        ColIdx.resize(nnzR);
        Val.resize(nnzR);
    }

    vector<INDEX_TYPE> RowIdx_COO;
    vector<INDEX_TYPE> ColIdx_COO;
    vector<VALUE_TYPE> Val_COO;
    vector<VALUE_TYPE> Col_X_COO;

    vector<VALUE_TYPE> X(n);
    vector<VALUE_TYPE> Y(m);
    vector<VALUE_TYPE> Y_CPU(m);
    vector<VALUE_TYPE> Y_CPU_Slice(m);
    vector<VALUE_TYPE> Y_Device(m);

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Data...";

    // 输入：filename/m/n/nnzR。
    // 输出：RowPtr/ColIdx/Val 被填成 CSR 三数组。
    if (!read_csr_dir) {
        Read_matrix_2_CSR(filename,
                          m,
                          n,
                          nnzR,
                          RowPtr,
                          ColIdx,
                          Val
                         );
    }

    cout << "  \t\tDone" << endl;

    if (m != n) {
        cerr << "[Jacobi] matrix must be square, got " << m << " x " << n << endl;
        return 1;
    }

    const INDEX_TYPE max_iters = EnvInt("MAX_ITERS", 1);
    const VALUE_TYPE tau = EnvFloat("TAU", 1.0e-5f);
    // B/Diag_inv 是只读向量；X 是 kernel 内外共享的解向量原地更新 buffer。
    // Matrix_data 后续只打包 R=A-D 的非对角部分。
    vector<VALUE_TYPE> B(n, 0.0f);
    vector<VALUE_TYPE> Diag_inv(n, 0.0f);
    vector<VALUE_TYPE> X_vec(n, 0.0f);
    vector<VALUE_TYPE> X_ref(n, 0.0f);
    vector<VALUE_TYPE> X_Device(n, 0.0f);
    VALUE_TYPE cpu_diff = 0.0f;
    bool has_input_b = false;

    if (read_csr_dir) {
        try {
            // CSR 目录存在 b.txt 时使用数据集自带 RHS；否则 PrepareJacobiVectorsAndR
            // 会用完整 A*ones 生成一个可校验 RHS。
            has_input_b = TryReadCsrVector(input_path / "b.txt", n, B);
        } catch (const std::exception& error) {
            cerr << "[Jacobi] " << error.what() << endl;
            return 1;
        }
    }

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Split A=D+R and prepare Jacobi vectors...";
    vector<INDEX_TYPE> R_RowPtr;
    vector<INDEX_TYPE> R_ColIdx;
    vector<VALUE_TYPE> R_Val;
    if (!PrepareJacobiVectorsAndR(n,
                                  RowPtr,
                                  ColIdx,
                                  Val,
                                  has_input_b,
                                  R_RowPtr,
                                  R_ColIdx,
                                  R_Val,
                                  B,
                                  Diag_inv,
                                  X_vec)) {
        return 1;
    }
    const INDEX_TYPE r_nnzR = static_cast<INDEX_TYPE>(R_Val.size());
    cout << "  \tDone" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi Config" << "] " << "B source: \t\t\t" << (has_input_b ? "dataset b.txt" : "A * ones") << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi Config" << "] " << "R NNZ: \t\t\t\t" << r_nnzR << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi Config" << "] " << "Max iters: \t\t\t" << max_iters << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi Config" << "] " << "Tau: \t\t\t\t" << tau << endl;

    // Cuper 的 host 预处理后续按“每个非零元”做 slice/PE/HBM 重排；
    // CSR 只有行指针，没有显式 RowIdx，所以先把 R 矩阵转成 COO 三数组。
    // 输入：m/n/r_nnzR + R_CSR(RowPtr, ColIdx, Val)。
    // 输出：R_COO(RowIdx_COO, ColIdx_COO, Val_COO)，三者长度都是 r_nnzR。
    RowIdx_COO.resize(r_nnzR);
    ColIdx_COO.resize(r_nnzR);
    Val_COO.resize(r_nnzR);
    Col_X_COO.resize(r_nnzR);
    CSR_2_COO(m,
              n,
              r_nnzR,
              R_RowPtr,
              R_ColIdx,
              R_Val,
              RowIdx_COO,
              ColIdx_COO,
              Val_COO
              );

#ifdef PINGPONG
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "PING-PONG Buffer \t\t\t" << "ON" << endl;
#else
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "PING-PONG Buffer \t\t\t" << "OFF" << endl;
#endif

#ifdef X_TABLE
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "X_TABLE \t\t\t\t" << "ON" << endl;
#else
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "X_TABLE \t\t\t\t" << "OFF" << endl;
#endif

#ifdef FLEX_REUSE
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "FLEX_REUSE \t\t\t" << "ON" << endl;
#else
    cout << "[" << setw(18) << setfill(' ') << "Optimisation" << "] " << "FLEX_REUSE \t\t\t" << "OFF" << endl;
#endif

    // 块大小由 Slice_SIZE 决定。当前 Cuper.h 中：
    //   Slice_SIZE = HBM_CHANNEL_NUM * ROW_HBM_NUM = 16 * 4 = 64
    // 所以后面的 SparseSlice 会把矩阵切成 64 x 64 的块。
    // 行/列方向的块数量分别是 ceil(m / Slice_SIZE)、ceil(n / Slice_SIZE)。
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Slice Size: \t\t\t" << Slice_SIZE << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Batch Size: \t\t\t" << BATCH_SIZE << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Iteration Num: \t\t\t" << ITERATION_NUM << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "HBM_Channel Num: \t\t\t" << HBM_CHANNEL_NUM << endl;

    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Create Slice Format...";

    // SparseSlice 是 host 侧的块稀疏中间格式：
    //   原始 COO -> 按 Slice_SIZE x Slice_SIZE 切块 -> 只保留非空块。
    // sliceVal 中每个 Matrix_COO 仍使用原矩阵的全局 row/col。
    // 相当于整张矩阵按块切分后的总览索引表 + 非空块数据仓库
    // 这里仍然只创建一个 sliceMatrix；它内部的 sliceVal 才是多个非空块。
    SparseSlice sliceMatrix;

    // 输入：矩阵尺寸 m/n/r_nnzR、块边长 Slice_SIZE、R 矩阵 COO 三数组。
    // 输出：sliceMatrix 被填成 R 矩阵的 SparseSlice 总容器。
    Create_SparseSlice(m,
                       n,
                       r_nnzR,
                       Slice_SIZE,
                       RowIdx_COO,
                       ColIdx_COO,
                       Val_COO,
                       sliceMatrix
                       );

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Slice Size: \t\t\t" << sliceMatrix.sliceSize << endl;
    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Slice Num:  \t\t\t" << sliceMatrix.numSlices << endl;

    cout << "[" << setw(18) << setfill(' ') << "Prepare Matrix" << "] " << "Preparing matrix R for FPGA...";

    vector<vector<SpElement> > SpElement_list_pes;
    vector<INDEX_TYPE>         SpElement_list_ptr;

    // 把 SparseSlice 中的每个非零元拆成 SpElement，并按物理 PE 分桶。
    //
    // SpElement_list_pes[p]：
    //   第 p 个 PE 要消费的矩阵元素列表。当前 NUM_PE = HBM_CHANNEL_NUM * PE_NUM，
    //   也就是 16 路 HBM * 每路 8 个 PE slot = 128。
    //
    // SpElement_list_ptr：
    //   batch 边界表，长度是 Batch_num + 1。kernel 通过它知道每个 column batch
    //   在各 PE list 中的起止位置。它是独立 HBM 输入，不混在 Matrix_data 里。
    //
    // 输入：128 个 PE、矩阵尺寸 m/n、slice/batch 参数和 sliceMatrix。
    // 输出：SpElement_list_pes 按 PE 分桶保存 SpElement；
    //       SpElement_list_ptr 保存每个 column batch 的起止边界。
    Create_SpElement_list_for_all_PEs(HBM_CHANNEL_NUM * PE_NUM,
                                      m,
                                      n,
                                      Slice_SIZE,
                                      BATCH_SIZE,
                                      sliceMatrix,
                                      SpElement_list_pes,
                                      SpElement_list_ptr,
                                      WINDOWS
                                     );

    aligned_vector<INDEX_TYPE> SpElement_list_ptr_fpga;
    // host buffer 长度按 16 和 1024 做对齐，满足 TAPA/XRT mmap 访问习惯。
    // 有效元素仍然只有 SpElement_list_ptr.size() 个；多出来的槽位补 0。
    INDEX_TYPE SpElement_list_ptr_fpga_size = ((SpElement_list_ptr.size() + 15) / 16) * 16;
    INDEX_TYPE SpElement_list_ptr_fpga_channel_size = ((SpElement_list_ptr_fpga_size + 1023) / 1024) * 1024;

    SpElement_list_ptr_fpga.resize(SpElement_list_ptr_fpga_channel_size, 0);

    for(INDEX_TYPE i = 0; i < SpElement_list_ptr.size(); ++i) {
        SpElement_list_ptr_fpga[i] = SpElement_list_ptr[i];
    }

    // Matrix_fpga_data 是真正的矩阵数据 HBM payload。
    // 它有 HBM_CHANNEL_NUM 路，每一路后面会 reinterpret 成 ap_uint<512>。
    // 每个 512-bit beat 内含 8 个 64-bit SpElement slot，分别给该 HBM channel
    // 下的 8 个 PE。
    // 也就是说，SpElement_list_pes 里的结构体元素会在下一步被压成 bit 字段，
    // 最终落到 Matrix_fpga_data[c] 里；kernel 端读取的是 Matrix_data[16]，
    // 不会再看到 C++ 的 SpElement 结构体。
    vector<aligned_vector<unsigned long> > Matrix_fpga_data(HBM_CHANNEL_NUM);

    // 输入：按 PE 分桶后的 SpElement_list_pes 和 batch 边界表。
    // 输出：Matrix_fpga_data[0..15]，也就是 16 路 HBM 的 packed 矩阵数据。
    // SpElement_list_ptr 不会被合进 Matrix_fpga_data，它作为 batch 边界表
    // 通过前面的 SpElement_list_ptr_fpga 单独传给 kernel。
    Create_SpElement_list_for_all_channels(SpElement_list_pes,
                                           SpElement_list_ptr,
                                           Matrix_fpga_data,
                                           HBM_CHANNEL_NUM
                                          );

    cout << "  \tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Jacobi On CPU" << "] " << "Run Jacobi reference...";
    auto start_cpu = std::chrono::steady_clock::now();
    const INDEX_TYPE cpu_iters = RunJacobiCpu(n,
                                              max_iters,
                                              tau,
                                              R_RowPtr,
                                              R_ColIdx,
                                              R_Val,
                                              B,
                                              Diag_inv,
                                              X_ref,
                                              cpu_diff);
    auto end_cpu = std::chrono::steady_clock::now();
    double time_cpu = std::chrono::duration_cast<std::chrono::nanoseconds>(end_cpu - start_cpu).count();
    time_cpu *= 1e-9;
    cout << "  \tDone" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi On CPU" << "] " << "Execution Time: \t\t\t" << time_cpu * 1000 << " ms" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi On CPU" << "] " << "Iterations: \t\t\t" << cpu_iters << endl;
    cout << "[" << setw(18) << setfill(' ') << "Jacobi On CPU" << "] " << "Final diff: \t\t\t" << cpu_diff << endl;

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Packing Jacobi vectors for FPGA...";

    // mmap buffer 同时按 float_v16 和 XRT/HBM 常见边界对齐。
    // 有效元素仍然只有前 n 个，后面的 padding lane 在 kernel update 中会被屏蔽。
    INDEX_TYPE vector_column_size = ((n + 16 - 1) / 16) * 16;
    INDEX_TYPE vector_channel_size = ((vector_column_size + 1023) / 1024) * 1024;
    aligned_vector<VALUE_TYPE> B_fpga_data(vector_channel_size, 0.0f);
    aligned_vector<VALUE_TYPE> Diag_inv_fpga_data(vector_channel_size, 0.0f);
    aligned_vector<VALUE_TYPE> X_fpga_data(vector_channel_size, 0.0f);

    for(INDEX_TYPE i = 0; i < n; ++i) {
        B_fpga_data[i] = B[i];
        Diag_inv_fpga_data[i] = Diag_inv[i];
        X_fpga_data[i] = X_vec[i];
    }

    aligned_vector<INDEX_TYPE> Status_fpga_data(16, 0);
    aligned_vector<double> Metrics_fpga_data(16, 0.0);
#ifdef JACOBI_TRACE_ENABLED
    aligned_vector<INDEX_TYPE> Debug_fpga_data(kJacobiTraceDebugWords, 0);
#endif
    for (INDEX_TYPE index = 0; index < static_cast<INDEX_TYPE>(Status_fpga_data.size()); ++index) {
        Status_fpga_data[index] = kJacobiStatusSentinelBase + index;
    }
    for (INDEX_TYPE index = 0; index < static_cast<INDEX_TYPE>(Metrics_fpga_data.size()); ++index) {
        Metrics_fpga_data[index] = kJacobiMetricsSentinelBase - static_cast<double>(index);
    }
#ifdef JACOBI_TRACE_ENABLED
    for (INDEX_TYPE index = 0; index < static_cast<INDEX_TYPE>(Debug_fpga_data.size()); ++index) {
        Debug_fpga_data[index] = kJacobiDebugSentinelBase + index;
    }
#endif
    cout << "  \tDone" << endl;

    // FPGA Jacobi
    // 顶层 CuperJacobiIteration(...) 的两个关键矩阵尺寸参数：
    //   Batch_num  = SpElement_list_ptr.size() - 1
    //   Matrix_len = SpElement_list_ptr[Batch_num]
    // Matrix_len 是每个 Matrix_data[channel] 实际读取的 512-bit beat 数。
    INDEX_TYPE SpElement_list_ptr_size = SpElement_list_ptr.size() - 1;
    INDEX_TYPE SpElement_list_ptr_max_len = SpElement_list_ptr[SpElement_list_ptr_size];
    cout << "[" << setw(18) << setfill(' ') << "Jacobi On FPGA" << "] " << "Run Jacobi On FPGA...";

    // 参数对应 CuperJacobiIteration(...) ABI：
    //   SpElement_list_ptr_fpga -> batch 边界表
    //   Matrix_fpga_data        -> 16 路 HBM 矩阵数据，reinterpret 为 ap_uint<512>
    //   B/Diag_inv/X           -> packed Jacobi 向量
    //   Status/Metrics         -> kernel 返回状态
#ifdef JACOBI_TRACE_ENABLED
    //   Debug                  -> 可选 trace/debug 槽位，只有宏打开时进入 ABI
    cout << "\n[jacobi-trace] enabled: Debug buffer ABI is active, mode="
#ifdef JACOBI_TRACE_FULL
         << "full"
#else
         << "light"
#endif
         << endl;
#endif
    double kernel_time = InvokeCuperJacobiIterationWithPrefinishDump(
                                      bitstream,
                                      Status_fpga_data,
                                      Metrics_fpga_data,
#ifdef JACOBI_TRACE_ENABLED
                                      Debug_fpga_data,
#endif
                                      tapa::read_only_mmap<INDEX_TYPE>(SpElement_list_ptr_fpga),
                                      tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(Matrix_fpga_data).reinterpret<ap_uint<512>>(),
                                      tapa::read_only_mmap<float>(B_fpga_data).reinterpret<float_v16>(),
                                      tapa::read_only_mmap<float>(Diag_inv_fpga_data).reinterpret<float_v16>(),
                                      tapa::read_write_mmap<float>(X_fpga_data).reinterpret<float_v16>(),
                                      tapa::read_write_mmap<INDEX_TYPE>(Status_fpga_data),
                                      tapa::read_write_mmap<double>(Metrics_fpga_data),
#ifdef JACOBI_TRACE_ENABLED
                                      tapa::read_write_mmap<INDEX_TYPE>(Debug_fpga_data),
#endif
                                      SpElement_list_ptr_size,
                                      SpElement_list_ptr_max_len,
                                      m,
                                      n,
                                      max_iters,
                                      tau
                                     );

    cout << " \t\tDone" << endl;
    kernel_time *= 1e-9;
    cout << "[" << setw(18) << "Jacobi On FPGA" << "] Execution Time: \t" << kernel_time * 1000 << " ms" << endl;
    cout << "[" << setw(18) << "Jacobi On FPGA" << "] Status: \t\t" << Status_fpga_data[0] << endl;
    cout << "[" << setw(18) << "Jacobi On FPGA" << "] Final buffer: \t" << Status_fpga_data[1] << endl;
    cout << "[" << setw(18) << "Jacobi On FPGA" << "] Iterations: \t\t" << Status_fpga_data[2] << endl;
    cout << "[" << setw(18) << "Jacobi On FPGA" << "] Final diff: \t\t" << Metrics_fpga_data[0] << endl;
    const double jacobi_clock_period_ns = EnvFloat("JACOBI_CLOCK_PERIOD_NS",
                                                   EnvFloat("CLOCK_PERIOD", 2.0f));
    cout << "[jacobi-timing-work] float_v16_packets=" << Metrics_fpga_data[2]
         << " spmv_update_packets=" << Metrics_fpga_data[3]
         << " iterations=" << Metrics_fpga_data[1] << endl;
    cout << "[jacobi-stage-cycles] spmv_update=" << std::fixed << std::setprecision(0)
         << Metrics_fpga_data[4]
         << " controller_total=" << Metrics_fpga_data[5]
         << " timer_total=" << Metrics_fpga_data[6]
         << " spmv_update_avg=" << Metrics_fpga_data[7]
         << std::defaultfloat << endl;
    cout << "[jacobi-stage-ms] spmv_update="
         << JacobiCyclesToMs(Metrics_fpga_data[4], jacobi_clock_period_ns)
         << " controller_total=" << JacobiCyclesToMs(Metrics_fpga_data[5], jacobi_clock_period_ns)
         << " timer_total=" << JacobiCyclesToMs(Metrics_fpga_data[6], jacobi_clock_period_ns)
         << " spmv_update_avg=" << JacobiCyclesToMs(Metrics_fpga_data[7], jacobi_clock_period_ns)
         << " clock_period_ns=" << jacobi_clock_period_ns << endl;
#ifdef JACOBI_TRACE_ENABLED
    PrintJacobiProbeSnapshot("jacobi-final-probe", Status_fpga_data, Metrics_fpga_data);
    PrintJacobiDebugBuffer(Debug_fpga_data);
#endif

    cout << "[" << setw(18) << "Verification" << "] Extracting Device Data...";
    for (INDEX_TYPE i = 0; i < n; i++) {
        X_Device[i] = X_fpga_data[i];
    }
    cout << " Done" << endl;

    cout << "--- Debug: First 16 elements comparison ---" << endl;
    const INDEX_TYPE debug_count = std::min<INDEX_TYPE>(n, 16);
    for(INDEX_TYPE i = 0; i < debug_count; ++i) {
        printf("Index [%d]: CPU=%f, Device=%f\n", i, X_ref[i], X_Device[i]);
    }

    INDEX_TYPE error_num = Verify_correctness(n, X_ref, X_Device, 1e-4);

    if(error_num == 0)
        cout << "[" << setw(18) << "Verification" << "] Correctness Verification: \tPassed" << endl;
    else
        cout << "[" << setw(18) << "Verification" << "] Correctness Verification: \tFailed" << endl;

    printf("[%18s] Error Num: %d, Error Percent: %.2f%%\n", "Verification", error_num, 100.0 * error_num / m);
}
