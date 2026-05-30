#include "cpu_reference.hpp"
#include "pcg_common.hpp"
#include "run_defaults.hpp"

#include <ap_int.h>
#include <tapa.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Cuper.h"
#include "Cuper_common.h"

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_ip.h"

// 主线 3：TAPA Cuper / FPGA-PCG。
//
// 这个 host 启动 DLC/Cuper/kernels/Cuper.cpp 里的 CuperPcg 顶层。
// 与 host/cuper_tapa_pcg_main.cpp 不同，CuperPcg 会在 FPGA/TAPA task graph
// 内完成初始 SpMV、PCG 迭代、alpha/beta、向量更新和收敛判断。
// host 只负责矩阵/向量打包、一次 kernel launch、结果读回和 CPU reference 校验。
namespace {

template <typename T>
using AlignedVector = std::vector<T, tapa::aligned_allocator<T>>;

using project_xplus::cgsolver::CpuReferenceResult;
using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::SolverConfig;
using project_xplus::cgsolver::data_t;

struct CliOptions {
    std::filesystem::path dataset_dir;
    std::string bitstream;
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = project_xplus::cgsolver::run_defaults::kMaxIters;
    double diff_tol = 1.0e-3;
    int kernel_timeout_sec = 60;
    // 旧标准 bitstream 只有 26 个 memory args，且 AP 是 double*。
    // 新 packed feed/AP demo 有 28 个 memory args。legacy_abi 让同一个
    // host 可以继续对比旧标准版。
    bool legacy_abi = false;
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [dataset_dir] [--bitstream path] [--tau value] [--max-iters value]"
              << " [--diff-tol value] [--kernel-timeout-sec value] [--legacy-abi]\n";
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);
    options.bitstream = env_or_empty("BITFILE");

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = std::filesystem::path(argv[index++]);
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--bitstream") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--bitstream requires a value");
            }
            options.bitstream = argv[++index];
        } else if (arg == "--tau") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--tau requires a value");
            }
            options.tau = std::stod(argv[++index]);
        } else if (arg == "--max-iters") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-iters requires a value");
            }
            options.max_iters = std::stoi(argv[++index]);
        } else if (arg == "--diff-tol") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--diff-tol requires a value");
            }
            options.diff_tol = std::stod(argv[++index]);
        } else if (arg == "--kernel-timeout-sec") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--kernel-timeout-sec requires a value");
            }
            options.kernel_timeout_sec = std::stoi(argv[++index]);
        } else if (arg == "--legacy-abi") {
            options.legacy_abi = true;
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
    if (options.kernel_timeout_sec < 0) {
        throw std::runtime_error("--kernel-timeout-sec must be non-negative");
    }

    return options;
}

int round_up(const int value, const int align) {
    return ((value + align - 1) / align) * align;
}

struct CuperTapaMatrix {
    // CuperPcg 和原 TAPA Cuper SpMV 复用同一套矩阵打包格式：
    // sp_element_list_ptr 给出 batch 边界，matrix_data[0..15] 对应 16 路 HBM。
    INDEX_TYPE batch_num = 0;
    INDEX_TYPE matrix_len = 0;
    AlignedVector<INDEX_TYPE> sp_element_list_ptr;
    std::vector<AlignedVector<unsigned long>> matrix_data;
    double plan_ms = 0.0;
};

struct PcgStageReport {
    const char* name;
    std::size_t work_metric_index;
    std::size_t cycle_metric_index;
};

struct PcgStageTimes {
    double init_spmv = 0.0;
    double init_zp = 0.0;
    double iter_spmv = 0.0;
    double dot_p_ap = 0.0;
    double update_xr = 0.0;
    double update_z = 0.0;
    double update_p = 0.0;
    double controller_total = 0.0;
    double timer_total = 0.0;

    double spmv_total() const {
        return init_spmv + iter_spmv;
    }

    double non_spmv_total() const {
        return init_zp + dot_p_ap + update_xr + update_z + update_p;
    }

    double accounted_stage_total() const {
        return spmv_total() + non_spmv_total();
    }
};

double elapsed_ms(const std::chrono::steady_clock::time_point start,
                  const std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

constexpr double kCuperTapaPcgKernelClockPeriodNs = 3.3;
constexpr double kCuperTapaPcgKernelClockMhz = 1000.0 / kCuperTapaPcgKernelClockPeriodNs;
constexpr std::array<PcgStageReport, 7> kPcgStageReports = {{
    // work_metric_index 对应 Metrics[5..14] 的手工 tick；
    // cycle_metric_index 对应 Metrics[16..24] 的 Pcg_Stage_Timer cycle。
    {"init_spmv", 5, 16},
    {"init_zp", 6, 17},
    {"iter_spmv", 7, 18},
    {"dot_p_ap", 14, 19},
    {"update_xr", 8, 20},
    {"update_z", 9, 21},
    {"update_p", 10, 22},
}};
double clock_mhz_to_period_ns(const double clock_mhz) {
    return 1000.0 / clock_mhz;
}

double cycles_to_ms(const double cycles, const double clock_period_ns) {
    return cycles * clock_period_ns * 1.0e-6;
}

double clamp_small_negative_ms(const double value) {
    return (value < 0.0 && value > -1.0e-9) ? 0.0 : value;
}

double read_data_clock_mhz_from_info(const std::filesystem::path& info_path) {
    std::ifstream input(info_path);
    if (!input) {
        return 0.0;
    }

    std::string line;
    bool in_data_clock_block = false;
    const std::regex frequency_pattern(R"(Frequency:\s*([0-9]+(?:\.[0-9]+)?)\s*MHz)");
    while (std::getline(input, line)) {
        if (line.find("Name:") != std::string::npos &&
            line.find("DATA_CLK") != std::string::npos) {
            in_data_clock_block = true;
            continue;
        }
        if (!in_data_clock_block) {
            continue;
        }

        std::smatch match;
        if (std::regex_search(line, match, frequency_pattern)) {
            return std::stod(match[1].str());
        }
        if (line.find("Name:") != std::string::npos) {
            in_data_clock_block = false;
        }
    }

    return 0.0;
}

double effective_stage_clock_mhz(const CliOptions& options) {
    if (!options.bitstream.empty()) {
        const std::filesystem::path info_path = std::filesystem::path(options.bitstream)
                                                    .concat(".info");
        const double data_clock_mhz = read_data_clock_mhz_from_info(info_path);
        if (data_clock_mhz > 0.0) {
            return data_clock_mhz;
        }
    }
    return kCuperTapaPcgKernelClockMhz;
}

const char* effective_stage_clock_source(const CliOptions& options) {
    if (!options.bitstream.empty()) {
        const std::filesystem::path info_path = std::filesystem::path(options.bitstream)
                                                    .concat(".info");
        if (read_data_clock_mhz_from_info(info_path) > 0.0) {
            return "xclbin_info_DATA_CLK";
        }
    }
    return "assumed_3p3ns";
}

constexpr std::array<int, 28> kCuperPcgMemoryGroups = {
    // 这个数组必须和 CuperPcg 顶层参数顺序一致，不是随便的 HBM 列表。
    // host 创建 BO 时按 arg_index 查 HBM bank；connectivity cfg 也必须同步。
    0,   // SpElement_list_ptr: HBM[0]
    0,   // Matrix_data_0: HBM[0]
    1,   // Matrix_data_1: HBM[1]
    2,   // Matrix_data_2: HBM[2]
    3,   // Matrix_data_3: HBM[3]
    4,   // Matrix_data_4: HBM[4]
    5,   // Matrix_data_5: HBM[5]
    6,   // Matrix_data_6: HBM[6]
    7,   // Matrix_data_7: HBM[7]
    8,   // Matrix_data_8: HBM[8]
    9,   // Matrix_data_9: HBM[9]
    10,  // Matrix_data_10: HBM[10]
    11,  // Matrix_data_11: HBM[11]
    12,  // Matrix_data_12: HBM[12]
    13,  // Matrix_data_13: HBM[13]
    14,  // Matrix_data_14: HBM[14]
    15,  // Matrix_data_15: HBM[15]
    16,  // B: HBM[16]
    17,  // M_inv: HBM[17]
    18,  // X: HBM[18]
    19,  // R: HBM[19]
    20,  // Z: HBM[20]
    21,  // P: HBM[21]
    22,  // AP_spmv: HBM[22], packed float_v16 SpMV output cache
    24,  // X_spmv: HBM[24], packed float_v16 feed for init A*x0
    25,  // P_spmv: HBM[25], packed float_v16 feed for iterative A*p
    26,  // Metrics: HBM[26]
    26,  // Status: HBM[26]
};

constexpr std::array<int, 26> kLegacyCuperPcgMemoryGroups = {
    // 旧标准 bitstream 的 ABI：没有 X_spmv/P_spmv/AP_spmv，AP 仍是 double*，
    // Metrics/Status 仍在 HBM[23]。
    0,   // SpElement_list_ptr: HBM[0]
    0,   // Matrix_data_0: HBM[0]
    1,   // Matrix_data_1: HBM[1]
    2,   // Matrix_data_2: HBM[2]
    3,   // Matrix_data_3: HBM[3]
    4,   // Matrix_data_4: HBM[4]
    5,   // Matrix_data_5: HBM[5]
    6,   // Matrix_data_6: HBM[6]
    7,   // Matrix_data_7: HBM[7]
    8,   // Matrix_data_8: HBM[8]
    9,   // Matrix_data_9: HBM[9]
    10,  // Matrix_data_10: HBM[10]
    11,  // Matrix_data_11: HBM[11]
    12,  // Matrix_data_12: HBM[12]
    13,  // Matrix_data_13: HBM[13]
    14,  // Matrix_data_14: HBM[14]
    15,  // Matrix_data_15: HBM[15]
    16,  // B: HBM[16]
    17,  // M_inv: HBM[17]
    18,  // X: HBM[18]
    19,  // R: HBM[19]
    20,  // Z: HBM[20]
    21,  // P: HBM[21]
    22,  // AP: HBM[22]
    23,  // Metrics: HBM[23]
    23,  // Status: HBM[23]
};

std::array<int, 28> cuper_pcg_memory_groups() {
    return kCuperPcgMemoryGroups;
}

xrt::ip open_cuper_pcg_ip(const xrt::device& device, const xrt::uuid& uuid) {
    const std::array<std::string, 3> names = {
        "CuperPcg:CuperPcg_1",
        "CuperPcg_1",
        "CuperPcg",
    };
    std::string errors;
    for (const std::string& name : names) {
        try {
            std::cerr << "[xplus-stage] trying xrt::ip name=" << name << "\n" << std::flush;
            return xrt::ip(device, uuid, name);
        } catch (const std::exception& error) {
            errors += " [" + name + ": " + error.what() + "]";
        }
    }
    throw std::runtime_error("failed to open CuperPcg xrt::ip:" + errors);
}

void write_reg_u64(xrt::ip& ip, const uint32_t offset, const uint64_t value) {
    // xrt::ip 的 register 写接口一次写 32 bit；64-bit BO 地址和 double
    // scalar 都要拆成低/高 32 bit。
    ip.write_register(offset, static_cast<uint32_t>(value));
    ip.write_register(offset + 4, static_cast<uint32_t>(value >> 32));
}

void write_reg_i32(xrt::ip& ip, const uint32_t offset, const int value) {
    ip.write_register(offset, static_cast<uint32_t>(value));
}

void write_reg_double(xrt::ip& ip, const uint32_t offset, const double value) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write_reg_u64(ip, offset, bits);
}

uint64_t read_reg_u64(const xrt::ip& ip, const uint32_t offset) {
    const uint64_t low = ip.read_register(offset);
    const uint64_t high = ip.read_register(offset + 4);
    return low | (high << 32);
}

CuperTapaMatrix build_tapa_matrix(const Dataset& dataset) {
    const auto start = std::chrono::steady_clock::now();
    CuperTapaMatrix matrix;

    // TAPA Cuper 原生工具链以 fp32 矩阵值和 COO/SparseSlice 为入口。
    // Project-XPlus 数据集是 double CSR，因此这里先做类型转换和格式转换。
    std::vector<INDEX_TYPE> row_ptr(dataset.row_ptr().begin(), dataset.row_ptr().end());
    std::vector<INDEX_TYPE> col_idx(dataset.col_idx().begin(), dataset.col_idx().end());
    std::vector<VALUE_TYPE> values(dataset.values().size(), 0.0f);
    for (std::size_t index = 0; index < dataset.values().size(); ++index) {
        values[index] = static_cast<VALUE_TYPE>(dataset.values()[index]);
    }

    std::vector<INDEX_TYPE> row_idx_coo(static_cast<std::size_t>(dataset.nnz()));
    std::vector<INDEX_TYPE> col_idx_coo(static_cast<std::size_t>(dataset.nnz()));
    std::vector<VALUE_TYPE> val_coo(static_cast<std::size_t>(dataset.nnz()));
    CSR_2_COO(dataset.n(),
              dataset.n(),
              dataset.nnz(),
              row_ptr,
              col_idx,
              values,
              row_idx_coo,
              col_idx_coo,
              val_coo);

    SparseSlice slice_matrix;
    Create_SparseSlice(dataset.n(),
                       dataset.n(),
                       dataset.nnz(),
                       Slice_SIZE,
                       row_idx_coo,
                       col_idx_coo,
                       val_coo,
                       slice_matrix);

    std::vector<std::vector<SpElement>> sp_element_list_pes;
    std::vector<INDEX_TYPE> sp_element_list_ptr;
    // CuperPcg 和原 TAPA Cuper 共用同一套矩阵打包：
    // CSR/COO -> SparseSlice -> SpElement list -> 16 路 HBM。
    // 注意 SpElement 里的 rowIdx 会在 Reordering 中变成 Cuper 内部
    // row 编码，不是原始全局 row；因此板上是否能过 65535 行，
    // 主要看 Cuper 调度/流控和 host 返回路径，而不是这里的 int 行号宽度。
    Create_SpElement_list_for_all_PEs(HBM_CHANNEL_NUM * PE_NUM,
                                      dataset.n(),
                                      dataset.n(),
                                      Slice_SIZE,
                                      BATCH_SIZE,
                                      slice_matrix,
                                      sp_element_list_pes,
                                      sp_element_list_ptr,
                                      WINDOWS);

    matrix.batch_num = static_cast<INDEX_TYPE>(sp_element_list_ptr.size()) - 1;
    matrix.matrix_len = sp_element_list_ptr[static_cast<std::size_t>(matrix.batch_num)];

    const INDEX_TYPE ptr_size = round_up(static_cast<int>(sp_element_list_ptr.size()), 16);
    const INDEX_TYPE ptr_channel_size = round_up(ptr_size, 1024);
    matrix.sp_element_list_ptr.assign(static_cast<std::size_t>(ptr_channel_size), 0);
    for (std::size_t index = 0; index < sp_element_list_ptr.size(); ++index) {
        matrix.sp_element_list_ptr[index] = sp_element_list_ptr[index];
    }

    matrix.matrix_data.resize(HBM_CHANNEL_NUM);
    Create_SpElement_list_for_all_channels(sp_element_list_pes,
                                           sp_element_list_ptr,
                                           matrix.matrix_data,
                                           HBM_CHANNEL_NUM);

    // plan_ms 只衡量 host 端矩阵打包时间，后面的 kernel_reported/kernel_wall
    // 才是 CuperPcg 自身运行时间。
    const auto end = std::chrono::steady_clock::now();
    matrix.plan_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return matrix;
}

template <typename T>
AlignedVector<T> aligned_copy(const std::vector<T>& input, int align = 1024) {
    // TAPA mmap 需要 aligned_allocator，并且向量长度按 1024 对齐，
    // 这样和原 Cuper host 的 buffer 约定保持一致。
    AlignedVector<T> output(static_cast<std::size_t>(round_up(static_cast<int>(input.size()), align)), T{});
    std::copy(input.begin(), input.end(), output.begin());
    return output;
}

AlignedVector<float_v16> pack_float_v16_from_double(const AlignedVector<double>& input,
                                                    const int valid_size,
                                                    const int align_packets = 1024) {
    // Cuper single SpMV 的 X 是 float_v16 packed HBM。CuperPcg 也维护这份
    // packed 副本，避免 kernel 内 controller 每次从 double X/P 标量打包。
    const int packet_count = (valid_size + 15) >> 4;
    AlignedVector<float_v16> output(
        static_cast<std::size_t>(round_up(packet_count, align_packets)));
    for (float_v16& packet : output) {
        for (int lane = 0; lane < 16; ++lane) {
            packet[lane] = 0.0f;
        }
    }

    for (int packet_index = 0; packet_index < packet_count; ++packet_index) {
        float_v16 packet;
        for (int lane = 0; lane < 16; ++lane) {
            const int index = (packet_index << 4) + lane;
            packet[lane] = index < valid_size ? static_cast<float>(input[static_cast<std::size_t>(index)])
                                               : 0.0f;
        }
        output[static_cast<std::size_t>(packet_index)] = packet;
    }
    return output;
}

AlignedVector<float_v16> make_zero_float_v16_packets(const int valid_size,
                                                     const int align_packets = 1024) {
    // AP_spmv/P_spmv 这类 packed 缓冲启动前需要清零，避免 padding lane
    // 参与软件仿真或调试读回时出现未初始化值。
    const int packet_count = (valid_size + 15) >> 4;
    AlignedVector<float_v16> output(
        static_cast<std::size_t>(round_up(packet_count, align_packets)));
    for (float_v16& packet : output) {
        for (int lane = 0; lane < 16; ++lane) {
            packet[lane] = 0.0f;
        }
    }
    return output;
}

template <typename T, std::size_t N>
xrt::bo make_input_bo(xrt::device& device,
                      const std::array<int, N>& memory_groups,
                      int arg_index,
                      const std::vector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), memory_groups[static_cast<std::size_t>(arg_index)]);
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T, std::size_t N>
xrt::bo make_input_bo(xrt::device& device,
                      const std::array<int, N>& memory_groups,
                      int arg_index,
                      const AlignedVector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), memory_groups[static_cast<std::size_t>(arg_index)]);
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T, std::size_t N>
xrt::bo make_inout_bo(xrt::device& device,
                      const std::array<int, N>& memory_groups,
                      int arg_index,
                      AlignedVector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), memory_groups[static_cast<std::size_t>(arg_index)]);
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T, std::size_t N>
xrt::bo make_output_bo(xrt::device& device,
                       const std::array<int, N>& memory_groups,
                       int arg_index,
                       AlignedVector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), memory_groups[static_cast<std::size_t>(arg_index)]);
    auto mapped = bo.map<T*>();
    std::fill(mapped, mapped + data.size(), T{});
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

double run_cuper_pcg_xrt_legacy(const CliOptions& options,
                                const Dataset& dataset,
                                const CuperTapaMatrix& matrix,
                                AlignedVector<double>& b,
                                AlignedVector<double>& minv,
                                AlignedVector<double>& x,
                                AlignedVector<double>& r,
                                AlignedVector<double>& z,
                                AlignedVector<double>& p,
                                AlignedVector<double>& ap,
                                AlignedVector<double>& metrics,
                                AlignedVector<INDEX_TYPE>& status,
                                const int effective_max_iters) {
    // legacy 路径只服务旧 26-arg CuperPcg bitstream。不要在这里加入
    // X_spmv/P_spmv/AP_spmv，否则会把旧 xclbin 的 register ABI 写错。
    std::cerr << "[xplus-stage] before xrt setup legacy_abi=1\n" << std::flush;
    const auto setup_start = std::chrono::steady_clock::now();
    xrt::device device(project_xplus::cgsolver::run_defaults::kDeviceIndex);
    auto uuid = device.load_xclbin(options.bitstream);
    const std::array<int, 26> memory_groups = kLegacyCuperPcgMemoryGroups;

    auto sp_ptr_bo = make_input_bo(device, memory_groups, 0, matrix.sp_element_list_ptr);
    auto matrix0_bo = make_input_bo(device, memory_groups, 1, matrix.matrix_data[0]);
    auto matrix1_bo = make_input_bo(device, memory_groups, 2, matrix.matrix_data[1]);
    auto matrix2_bo = make_input_bo(device, memory_groups, 3, matrix.matrix_data[2]);
    auto matrix3_bo = make_input_bo(device, memory_groups, 4, matrix.matrix_data[3]);
    auto matrix4_bo = make_input_bo(device, memory_groups, 5, matrix.matrix_data[4]);
    auto matrix5_bo = make_input_bo(device, memory_groups, 6, matrix.matrix_data[5]);
    auto matrix6_bo = make_input_bo(device, memory_groups, 7, matrix.matrix_data[6]);
    auto matrix7_bo = make_input_bo(device, memory_groups, 8, matrix.matrix_data[7]);
    auto matrix8_bo = make_input_bo(device, memory_groups, 9, matrix.matrix_data[8]);
    auto matrix9_bo = make_input_bo(device, memory_groups, 10, matrix.matrix_data[9]);
    auto matrix10_bo = make_input_bo(device, memory_groups, 11, matrix.matrix_data[10]);
    auto matrix11_bo = make_input_bo(device, memory_groups, 12, matrix.matrix_data[11]);
    auto matrix12_bo = make_input_bo(device, memory_groups, 13, matrix.matrix_data[12]);
    auto matrix13_bo = make_input_bo(device, memory_groups, 14, matrix.matrix_data[13]);
    auto matrix14_bo = make_input_bo(device, memory_groups, 15, matrix.matrix_data[14]);
    auto matrix15_bo = make_input_bo(device, memory_groups, 16, matrix.matrix_data[15]);
    auto b_bo = make_input_bo(device, memory_groups, 17, b);
    auto minv_bo = make_input_bo(device, memory_groups, 18, minv);
    auto x_bo = make_inout_bo(device, memory_groups, 19, x);
    auto r_bo = make_inout_bo(device, memory_groups, 20, r);
    auto z_bo = make_inout_bo(device, memory_groups, 21, z);
    auto p_bo = make_inout_bo(device, memory_groups, 22, p);
    auto ap_bo = make_inout_bo(device, memory_groups, 23, ap);
    auto metrics_bo = make_output_bo(device, memory_groups, 24, metrics);
    auto status_bo = make_output_bo(device, memory_groups, 25, status);
    const auto setup_end = std::chrono::steady_clock::now();
    std::cerr << "[xplus-stage] after xrt setup ms=" << elapsed_ms(setup_start, setup_end) << "\n"
              << std::flush;

    std::cerr << "[xplus-stage] before xrt direct register start\n" << std::flush;
    const auto kernel_start = std::chrono::steady_clock::now();
    xrt::ip ip = open_cuper_pcg_ip(device, uuid);
    const std::array<std::pair<uint32_t, uint64_t>, 26> pointer_args = {{
        // 旧 ABI register offset：每个 m_axi 指针占 0x0c 间隔，scalar
        // 从 0x148 开始。这个布局来自旧 xclbin 的 kernel.xml。
        {0x010, sp_ptr_bo.address()},
        {0x01c, matrix0_bo.address()},
        {0x028, matrix1_bo.address()},
        {0x034, matrix2_bo.address()},
        {0x040, matrix3_bo.address()},
        {0x04c, matrix4_bo.address()},
        {0x058, matrix5_bo.address()},
        {0x064, matrix6_bo.address()},
        {0x070, matrix7_bo.address()},
        {0x07c, matrix8_bo.address()},
        {0x088, matrix9_bo.address()},
        {0x094, matrix10_bo.address()},
        {0x0a0, matrix11_bo.address()},
        {0x0ac, matrix12_bo.address()},
        {0x0b8, matrix13_bo.address()},
        {0x0c4, matrix14_bo.address()},
        {0x0d0, matrix15_bo.address()},
        {0x0dc, b_bo.address()},
        {0x0e8, minv_bo.address()},
        {0x0f4, x_bo.address()},
        {0x100, r_bo.address()},
        {0x10c, z_bo.address()},
        {0x118, p_bo.address()},
        {0x124, ap_bo.address()},
        {0x130, metrics_bo.address()},
        {0x13c, status_bo.address()},
    }};
    for (const auto& [offset, address] : pointer_args) {
        write_reg_u64(ip, offset, address);
    }
    write_reg_i32(ip, 0x148, matrix.batch_num);
    write_reg_i32(ip, 0x150, matrix.matrix_len);
    write_reg_i32(ip, 0x158, dataset.n());
    write_reg_i32(ip, 0x160, dataset.n());
    write_reg_i32(ip, 0x168, effective_max_iters);
    write_reg_double(ip, 0x170, options.tau);

    const uint32_t before_ctrl = ip.read_register(0x00);
    const uint32_t read_batch_num = ip.read_register(0x148);
    const uint32_t read_matrix_len = ip.read_register(0x150);
    const uint32_t read_row_num = ip.read_register(0x158);
    const uint64_t read_sp_ptr = read_reg_u64(ip, 0x010);
    const uint64_t read_x_ptr = read_reg_u64(ip, 0x0f4);
    std::cerr << "[xplus-stage] direct ctrl before start=0x" << std::hex << before_ctrl
              << std::dec << "\n"
              << std::flush;
    std::cerr << "[xplus-stage] direct arg readback batch=" << read_batch_num
              << " matrix_len=" << read_matrix_len
              << " row=" << read_row_num
              << " sp_ptr=0x" << std::hex << read_sp_ptr
              << " x=0x" << read_x_ptr << std::dec << "\n"
              << std::flush;
    ip.write_register(0x00, 0x01);

    bool completed = false;
    uint32_t last_ctrl = 0;
    auto next_report = kernel_start + std::chrono::seconds(5);
    while (true) {
        last_ctrl = ip.read_register(0x00);
        const bool done = (last_ctrl & 0x02) != 0;
        const bool idle = (last_ctrl & 0x04) != 0;
        if (done || idle) {
            completed = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report) {
            std::cerr << "[xplus-stage] direct wait ms=" << elapsed_ms(kernel_start, now)
                      << " ctrl=0x" << std::hex << last_ctrl << std::dec << "\n"
                      << std::flush;
            next_report = now + std::chrono::seconds(5);
        }
        if (options.kernel_timeout_sec > 0 &&
            now - kernel_start > std::chrono::seconds(options.kernel_timeout_sec)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!completed) {
        throw std::runtime_error("direct register run did not complete, ctrl=0x" +
                                 std::to_string(last_ctrl));
    }
    const auto kernel_end = std::chrono::steady_clock::now();
    std::cerr << "[xplus-stage] after xrt direct register wait ctrl=0x" << std::hex
              << last_ctrl << std::dec << "\n"
              << std::flush;

    x_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, x.size() * sizeof(double), 0);
    metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(double), 0);
    status_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, status.size() * sizeof(INDEX_TYPE), 0);
    const auto x_mapped = x_bo.map<double*>();
    const auto metrics_mapped = metrics_bo.map<double*>();
    const auto status_mapped = status_bo.map<INDEX_TYPE*>();
    std::copy(x_mapped, x_mapped + x.size(), x.begin());
    std::copy(metrics_mapped, metrics_mapped + metrics.size(), metrics.begin());
    std::copy(status_mapped, status_mapped + status.size(), status.begin());
    std::cerr << "[xplus-stage] after xrt readback\n" << std::flush;
    return elapsed_ms(kernel_start, kernel_end);
}

double run_cuper_pcg_xrt(const CliOptions& options,
                         const Dataset& dataset,
                         const CuperTapaMatrix& matrix,
                         AlignedVector<double>& b,
                         AlignedVector<double>& minv,
                         AlignedVector<double>& x,
                         AlignedVector<double>& r,
                         AlignedVector<double>& z,
                         AlignedVector<double>& p,
                         AlignedVector<double>& legacy_ap,
                         AlignedVector<float_v16>& ap_spmv,
                         AlignedVector<float_v16>& x_spmv,
                         AlignedVector<float_v16>& p_spmv,
                         AlignedVector<double>& metrics,
                         AlignedVector<INDEX_TYPE>& status,
                         const int effective_max_iters) {
    if (options.legacy_abi) {
        return run_cuper_pcg_xrt_legacy(options,
                                        dataset,
                                        matrix,
                                        b,
                                        minv,
                                        x,
                                        r,
                                        z,
                                        p,
                                        legacy_ap,
                                        metrics,
                                        status,
                                        effective_max_iters);
    }

    std::cerr << "[xplus-stage] before xrt setup\n" << std::flush;
    const auto setup_start = std::chrono::steady_clock::now();
    xrt::device device(project_xplus::cgsolver::run_defaults::kDeviceIndex);
    auto uuid = device.load_xclbin(options.bitstream);
    const std::array<int, 28> memory_groups = cuper_pcg_memory_groups();

    auto sp_ptr_bo = make_input_bo(device, memory_groups, 0, matrix.sp_element_list_ptr);
    auto matrix0_bo = make_input_bo(device, memory_groups, 1, matrix.matrix_data[0]);
    auto matrix1_bo = make_input_bo(device, memory_groups, 2, matrix.matrix_data[1]);
    auto matrix2_bo = make_input_bo(device, memory_groups, 3, matrix.matrix_data[2]);
    auto matrix3_bo = make_input_bo(device, memory_groups, 4, matrix.matrix_data[3]);
    auto matrix4_bo = make_input_bo(device, memory_groups, 5, matrix.matrix_data[4]);
    auto matrix5_bo = make_input_bo(device, memory_groups, 6, matrix.matrix_data[5]);
    auto matrix6_bo = make_input_bo(device, memory_groups, 7, matrix.matrix_data[6]);
    auto matrix7_bo = make_input_bo(device, memory_groups, 8, matrix.matrix_data[7]);
    auto matrix8_bo = make_input_bo(device, memory_groups, 9, matrix.matrix_data[8]);
    auto matrix9_bo = make_input_bo(device, memory_groups, 10, matrix.matrix_data[9]);
    auto matrix10_bo = make_input_bo(device, memory_groups, 11, matrix.matrix_data[10]);
    auto matrix11_bo = make_input_bo(device, memory_groups, 12, matrix.matrix_data[11]);
    auto matrix12_bo = make_input_bo(device, memory_groups, 13, matrix.matrix_data[12]);
    auto matrix13_bo = make_input_bo(device, memory_groups, 14, matrix.matrix_data[13]);
    auto matrix14_bo = make_input_bo(device, memory_groups, 15, matrix.matrix_data[14]);
    auto matrix15_bo = make_input_bo(device, memory_groups, 16, matrix.matrix_data[15]);
    auto b_bo = make_input_bo(device, memory_groups, 17, b);
    auto minv_bo = make_input_bo(device, memory_groups, 18, minv);
    auto x_bo = make_inout_bo(device, memory_groups, 19, x);
    auto r_bo = make_inout_bo(device, memory_groups, 20, r);
    auto z_bo = make_inout_bo(device, memory_groups, 21, z);
    auto p_bo = make_inout_bo(device, memory_groups, 22, p);
    auto ap_spmv_bo = make_inout_bo(device, memory_groups, 23, ap_spmv);
    auto x_spmv_bo = make_input_bo(device, memory_groups, 24, x_spmv);
    auto p_spmv_bo = make_inout_bo(device, memory_groups, 25, p_spmv);
    auto metrics_bo = make_output_bo(device, memory_groups, 26, metrics);
    auto status_bo = make_output_bo(device, memory_groups, 27, status);
    const auto setup_end = std::chrono::steady_clock::now();
    std::cerr << "[xplus-stage] after xrt setup ms=" << elapsed_ms(setup_start, setup_end) << "\n"
              << std::flush;

    std::cerr << "[xplus-stage] before xrt direct register start\n" << std::flush;
    const auto kernel_start = std::chrono::steady_clock::now();
    xrt::ip ip = open_cuper_pcg_ip(device, uuid);
    const std::array<std::pair<uint32_t, uint64_t>, 28> pointer_args = {{
        // 新 ABI register offset：AP_spmv/X_spmv/P_spmv 插入到 AP 后面，
        // Metrics/Status 和 scalar 参数整体后移。若 CuperPcg 参数顺序变化，
        // 这里和 kCuperPcgMemoryGroups/connectivity 必须一起改。
        {0x010, sp_ptr_bo.address()},
        {0x01c, matrix0_bo.address()},
        {0x028, matrix1_bo.address()},
        {0x034, matrix2_bo.address()},
        {0x040, matrix3_bo.address()},
        {0x04c, matrix4_bo.address()},
        {0x058, matrix5_bo.address()},
        {0x064, matrix6_bo.address()},
        {0x070, matrix7_bo.address()},
        {0x07c, matrix8_bo.address()},
        {0x088, matrix9_bo.address()},
        {0x094, matrix10_bo.address()},
        {0x0a0, matrix11_bo.address()},
        {0x0ac, matrix12_bo.address()},
        {0x0b8, matrix13_bo.address()},
        {0x0c4, matrix14_bo.address()},
        {0x0d0, matrix15_bo.address()},
        {0x0dc, b_bo.address()},
        {0x0e8, minv_bo.address()},
        {0x0f4, x_bo.address()},
        {0x100, r_bo.address()},
        {0x10c, z_bo.address()},
        {0x118, p_bo.address()},
        {0x124, ap_spmv_bo.address()},
        {0x130, x_spmv_bo.address()},
        {0x13c, p_spmv_bo.address()},
        {0x148, metrics_bo.address()},
        {0x154, status_bo.address()},
    }};
    for (const auto& [offset, address] : pointer_args) {
        write_reg_u64(ip, offset, address);
    }
    write_reg_i32(ip, 0x160, matrix.batch_num);
    write_reg_i32(ip, 0x168, matrix.matrix_len);
    write_reg_i32(ip, 0x170, dataset.n());
    write_reg_i32(ip, 0x178, dataset.n());
    write_reg_i32(ip, 0x180, effective_max_iters);
    write_reg_double(ip, 0x188, options.tau);

    const uint32_t before_ctrl = ip.read_register(0x00);
    const uint32_t read_batch_num = ip.read_register(0x160);
    const uint32_t read_matrix_len = ip.read_register(0x168);
    const uint32_t read_row_num = ip.read_register(0x170);
    const uint64_t read_sp_ptr = read_reg_u64(ip, 0x010);
    const uint64_t read_x_ptr = read_reg_u64(ip, 0x0f4);
    std::cerr << "[xplus-stage] direct ctrl before start=0x" << std::hex << before_ctrl
              << std::dec << "\n"
              << std::flush;
    std::cerr << "[xplus-stage] direct arg readback batch=" << read_batch_num
              << " matrix_len=" << read_matrix_len
              << " row=" << read_row_num
              << " sp_ptr=0x" << std::hex << read_sp_ptr
              << " x=0x" << read_x_ptr << std::dec << "\n"
              << std::flush;
    ip.write_register(0x00, 0x01);

    bool completed = false;
    uint32_t last_ctrl = 0;
    auto next_report = kernel_start + std::chrono::seconds(5);
    while (true) {
        last_ctrl = ip.read_register(0x00);
        const bool done = (last_ctrl & 0x02) != 0;
        const bool idle = (last_ctrl & 0x04) != 0;
        if (done || idle) {
            completed = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report) {
            std::cerr << "[xplus-stage] direct wait ms=" << elapsed_ms(kernel_start, now)
                      << " ctrl=0x" << std::hex << last_ctrl << std::dec << "\n"
                      << std::flush;
            next_report = now + std::chrono::seconds(5);
        }
        if (options.kernel_timeout_sec > 0 &&
            now - kernel_start > std::chrono::seconds(options.kernel_timeout_sec)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!completed) {
        throw std::runtime_error("direct register run did not complete, ctrl=0x" +
                                 std::to_string(last_ctrl));
    }
    const auto kernel_end = std::chrono::steady_clock::now();
    std::cerr << "[xplus-stage] after xrt direct register wait ctrl=0x" << std::hex
              << last_ctrl << std::dec << "\n"
              << std::flush;

    x_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, x.size() * sizeof(double), 0);
    metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(double), 0);
    status_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, status.size() * sizeof(INDEX_TYPE), 0);
    const auto x_mapped = x_bo.map<double*>();
    const auto metrics_mapped = metrics_bo.map<double*>();
    const auto status_mapped = status_bo.map<INDEX_TYPE*>();
    std::copy(x_mapped, x_mapped + x.size(), x.begin());
    std::copy(metrics_mapped, metrics_mapped + metrics.size(), metrics.begin());
    std::copy(status_mapped, status_mapped + status.size(), status.begin());
    std::cerr << "[xplus-stage] after xrt readback\n" << std::flush;
    return elapsed_ms(kernel_start, kernel_end);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = std::chrono::steady_clock::now();
        const CliOptions options = parse_args(argc, argv);
        const Dataset dataset = Dataset::load(options.dataset_dir);

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;
        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        CuperTapaMatrix matrix = build_tapa_matrix(dataset);
        std::vector<data_t> minv_host = project_xplus::cgsolver::build_jacobi_inverse(dataset);

        // 这些 BO/mmap 是 FPGA 内 PCG 的完整状态：
        // B/M_inv 是只读输入，X/R/Z/P 是 kernel 内迭代更新的 FP64
        // 向量；AP_spmv/X_spmv/P_spmv 是贴近 Cuper SpMV 的 packed
        // float_v16 缓冲；Metrics/Status 是收敛状态和调试信息的最小写回口。
        AlignedVector<double> b = aligned_copy(dataset.b());
        AlignedVector<double> minv = aligned_copy(minv_host);
        AlignedVector<double> x = aligned_copy(dataset.x0());
        AlignedVector<double> r(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> z(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> p(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> legacy_ap(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<float_v16> ap_spmv = make_zero_float_v16_packets(dataset.n());
        AlignedVector<float_v16> x_spmv = pack_float_v16_from_double(x, dataset.n());
        AlignedVector<float_v16> p_spmv = make_zero_float_v16_packets(dataset.n());
        AlignedVector<double> metrics(1024, 0.0);
        AlignedVector<INDEX_TYPE> status(1024, 0);

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-pcg-tapa-fpga"
                  << " kernel=CuperPcg"
                  << " bitstream=" << (options.bitstream.empty() ? "<software-sim>" : options.bitstream)
                  << " batches=" << matrix.batch_num
                  << " matrix_len=" << matrix.matrix_len << "\n"
                  << std::flush;

        const auto kernel_start = std::chrono::steady_clock::now();
        double kernel_reported_ms = 0.0;
        if (options.bitstream.empty()) {
            // 无 bitstream 时仍走 TAPA 软件仿真，便于验证 CuperPcg 任务图逻辑。
            // 这里的参数顺序同样要与 CuperPcg 顶层完全一致；软件仿真能
            // 提前发现 ABI 类型或顺序错误。
            std::cerr << "[xplus-stage] before tapa::invoke\n" << std::flush;
            const double kernel_ns = tapa::invoke(
                CuperPcg,
                options.bitstream,
                tapa::read_only_mmap<INDEX_TYPE>(matrix.sp_element_list_ptr),
                tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(matrix.matrix_data)
                    .reinterpret<ap_uint<512>>(),
                tapa::read_only_mmap<double>(b),
                tapa::read_only_mmap<double>(minv),
                tapa::read_write_mmap<double>(x),
                tapa::read_write_mmap<double>(r),
                tapa::read_write_mmap<double>(z),
                tapa::read_write_mmap<double>(p),
                tapa::read_write_mmap<float_v16>(ap_spmv),
                tapa::read_only_mmap<float_v16>(x_spmv),
                tapa::read_write_mmap<float_v16>(p_spmv),
                tapa::write_only_mmap<double>(metrics),
                tapa::write_only_mmap<INDEX_TYPE>(status),
                matrix.batch_num,
                matrix.matrix_len,
                dataset.n(),
                dataset.n(),
                effective_max_iters,
                config.tau);
            kernel_reported_ms = kernel_ns * 1.0e-6;
            std::cerr << "[xplus-stage] after tapa::invoke\n" << std::flush;
        } else {
            kernel_reported_ms = run_cuper_pcg_xrt(options,
                                                   dataset,
                                                   matrix,
                                                   b,
                                                   minv,
                                                   x,
                                                   r,
                                                   z,
                                                   p,
                                                   legacy_ap,
                                                   ap_spmv,
                                                   x_spmv,
                                                   p_spmv,
                                                   metrics,
                                                   status,
                                                   effective_max_iters);
        }
        const auto kernel_end = std::chrono::steady_clock::now();

        // kernel 返回后 X 即为最终解向量。只拷贝有效 n 项，
        // 对齐 padding 不参与残差和 diff 校验。
        std::vector<double> solution(static_cast<std::size_t>(dataset.n()), 0.0);
        std::copy(x.begin(), x.begin() + dataset.n(), solution.begin());

        const CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);
        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < solution.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double abs_diff = std::fabs(solution[index] - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }

        const double residual_l2 =
            project_xplus::cgsolver::compute_residual_norm(dataset, solution);
        const auto total_end = std::chrono::steady_clock::now();

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << status[1]
                  << " residual_abs=" << residual_l2
                  << " status=" << (status[0] == 0 ? "converged"
                                   : status[0] == 1 ? "max_iter"
                                                    : "breakdown")
                  << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_tapa_pcg_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol
                  << " rr=" << metrics[1] << "\n";
        std::cout << "[timing-ms] plan=" << matrix.plan_ms
                  << " kernel_reported=" << kernel_reported_ms
                  << " kernel_wall="
                  << std::chrono::duration<double, std::milli>(kernel_end - kernel_start).count()
                  << " total="
                  << std::chrono::duration<double, std::milli>(total_end - total_start).count()
                  << "\n";
        const double stage_clock_mhz = effective_stage_clock_mhz(options);
        const double stage_clock_period_ns = clock_mhz_to_period_ns(stage_clock_mhz);
        const char* stage_clock_source = effective_stage_clock_source(options);
        PcgStageTimes stage_times;
        stage_times.init_spmv = cycles_to_ms(metrics[16], stage_clock_period_ns);
        stage_times.init_zp = cycles_to_ms(metrics[17], stage_clock_period_ns);
        stage_times.iter_spmv = cycles_to_ms(metrics[18], stage_clock_period_ns);
        stage_times.dot_p_ap = cycles_to_ms(metrics[19], stage_clock_period_ns);
        stage_times.update_xr = cycles_to_ms(metrics[20], stage_clock_period_ns);
        stage_times.update_z = cycles_to_ms(metrics[21], stage_clock_period_ns);
        stage_times.update_p = cycles_to_ms(metrics[22], stage_clock_period_ns);
        stage_times.controller_total = cycles_to_ms(metrics[23], stage_clock_period_ns);
        stage_times.timer_total = cycles_to_ms(metrics[24], stage_clock_period_ns);

        std::cout << std::fixed << std::setprecision(0);
        std::cout << "[stage-work-ticks]"
                  << " packet_count=" << metrics[4];
        for (const PcgStageReport& stage : kPcgStageReports) {
            std::cout << " " << stage.name << "=" << metrics[stage.work_metric_index];
        }
        std::cout << " accounted_total=" << metrics[11]
                  << " row_num=" << metrics[12]
                  << " max_iters=" << metrics[13]
                  << "\n";
        std::cout << "[stage-cycles]"
                  << " clock_mhz=" << std::setprecision(6)
                  << stage_clock_mhz
                  << " clock_source=" << stage_clock_source;
        for (const PcgStageReport& stage : kPcgStageReports) {
            std::cout << " " << stage.name << "=" << std::setprecision(0)
                      << metrics[stage.cycle_metric_index];
        }
        std::cout << " controller_total=" << metrics[23]
                  << " timer_total=" << metrics[24]
                  << "\n";
        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[stage-ms]"
                  << " init_spmv=" << stage_times.init_spmv
                  << " init_zp=" << stage_times.init_zp
                  << " iter_spmv=" << stage_times.iter_spmv
                  << " dot_p_ap=" << stage_times.dot_p_ap
                  << " update_xr=" << stage_times.update_xr
                  << " update_z=" << stage_times.update_z
                  << " update_p=" << stage_times.update_p
                  << " controller_total=" << stage_times.controller_total
                  << " timer_total=" << stage_times.timer_total
                  << "\n";
        const double pcg_spmv_calls = 1.0 + static_cast<double>(status[1]);
        // metrics[18]/iter_spmv 已经是所有 PCG 迭代 A*p 的累计时间；
        // 多迭代报告只用它本身，不能再乘 Status[1]。
        const double pcg_spmv_total_ms = stage_times.spmv_total();
        const double pcg_spmv_avg_ms = pcg_spmv_calls > 0.0
                                           ? pcg_spmv_total_ms / pcg_spmv_calls
                                           : 0.0;
        const double pcg_spmv_seconds = pcg_spmv_total_ms * 1.0e-3;
        const double pcg_spmv_gflops = pcg_spmv_seconds > 0.0
                                           ? (2.0 * static_cast<double>(dataset.nnz()) *
                                              pcg_spmv_calls) /
                                                 pcg_spmv_seconds / 1.0e9
                                           : 0.0;
        std::cout << "[pcg-spmv-ms]"
                  << " clock_mhz=" << stage_clock_mhz
                  << " clock_source=" << stage_clock_source
                  << " init_spmv=" << stage_times.init_spmv
                  << " iter_spmv=" << stage_times.iter_spmv
                  << " spmv_total=" << pcg_spmv_total_ms
                  << " spmv_calls=" << std::defaultfloat << pcg_spmv_calls
                  << std::scientific
                  << " spmv_avg=" << pcg_spmv_avg_ms
                  << " gflops=" << pcg_spmv_gflops
                  << "\n";
        const double pcg_non_spmv_total_ms = stage_times.non_spmv_total();
        const double accounted_stage_total_ms = stage_times.accounted_stage_total();
        const double unaccounted_controller_ms =
            clamp_small_negative_ms(stage_times.controller_total - accounted_stage_total_ms);
        const double kernel_minus_controller_ms =
            clamp_small_negative_ms(kernel_reported_ms - stage_times.controller_total);
        std::cout << "[pcg-control-ms]"
                  << " clock_mhz=" << stage_clock_mhz
                  << " clock_source=" << stage_clock_source
                  << " controller_total=" << stage_times.controller_total
                  << " spmv_total=" << pcg_spmv_total_ms
                  << " pcg_non_spmv_total=" << pcg_non_spmv_total_ms
                  << " init_zp=" << stage_times.init_zp
                  << " dot_p_ap=" << stage_times.dot_p_ap
                  << " update_xr=" << stage_times.update_xr
                  << " update_z=" << stage_times.update_z
                  << " update_p=" << stage_times.update_p
                  << " accounted_stage_total=" << accounted_stage_total_ms
                  << " unaccounted_controller=" << unaccounted_controller_ms
                  << " kernel_reported=" << kernel_reported_ms
                  << " kernel_minus_controller=" << kernel_minus_controller_ms
                  << "\n";

        // Short benchmark runs intentionally use small MAX_ITERS values; max_iter
        // is valid as long as the result matches the CPU reference with the same cap.
        if (status[0] != 0 && status[0] != 1) {
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
