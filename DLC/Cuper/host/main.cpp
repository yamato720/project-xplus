#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <bitset>
#include <iomanip>
#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"
#include "Cuper_common.h"

using namespace std;

// TAPA mmap 的 host buffer 需要使用 aligned_allocator。
// 这个别名用于 SpElement_list_ptr、Matrix_data、X/Y 等要传给 kernel 的数组。
template <typename T>
using aligned_vector = std::vector<T, tapa::aligned_allocator<T> >;

int main(int argc, char* argv[]) {

    // 这个 host 是 Cuper 子项目的 standalone SpMV 入口，只接收一个矩阵文件。
    // 上层 Project-XPlus 的 single SpMV 主线入口在 host/cuper_tapa_pcg_main.cpp。
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <matrix_file>" << endl;
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

    INDEX_TYPE m, n, nnzR, isSymmetric;

#ifdef BINARY_READ
    // binary 读入路径：一次性读取矩阵尺寸和 CSR 数据。
    // CSR 三元组含义：
    //   RowPtr[i]..RowPtr[i + 1] 是第 i 行非零元范围；
    //   ColIdx[j] 是非零元列号；
    //   Val[j] 是非零元值。
    vector<INDEX_TYPE> RowPtr;
    vector<INDEX_TYPE> ColIdx;
    vector<VALUE_TYPE> Val;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read binary: \t\t\t" << "ON" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Size...";

    // 输入：filename。
    // 输出：m/n/nnzR/isSymmetric 和 CSR(RowPtr, ColIdx, Val) 都由函数填充。
    Read_binary_matrix_2_CSR(filename,
                             m,
                             n,
                             nnzR,
                             isSymmetric,
                             RowPtr,
                             ColIdx,
                             Val
                            );
    cout << "  \t\tDone" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Matrix Size: \t\t\t" << m << " x " << n << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "NNZ: \t\t\t\t" << nnzR << endl;
   
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Data...";
    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Allocate Memory Space...";

    vector<INDEX_TYPE> RowIdx_COO(nnzR);
    vector<INDEX_TYPE> ColIdx_COO(nnzR);
    vector<VALUE_TYPE> Val_COO(nnzR);
    vector<VALUE_TYPE> Col_X_COO(nnzR);

    vector<VALUE_TYPE> X(n);
    vector<VALUE_TYPE> Y(m);
    vector<VALUE_TYPE> Y_CPU(m);
    vector<VALUE_TYPE> Y_CPU_Slice(m);
    vector<VALUE_TYPE> Y_Device(m);
#else
    // Matrix Market 读入路径：先读尺寸，再分配 CSR/COO/向量空间。
    // 这里的 VALUE_TYPE/INDEX_TYPE 来自 Cuper.h，当前分别是 float/int。
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read binary: \t\t\t" << "OFF" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Size...";

    Read_matrix_size(filename, &m, &n, &nnzR, &isSymmetric);

    cout << "  \t\tDone" << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Matrix Size: \t\t\t" << m << " x " << n << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "NNZ: \t\t\t\t" << nnzR << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Allocate Memory Space...";

    vector<INDEX_TYPE> RowPtr(m + 1);
    vector<INDEX_TYPE> ColIdx(nnzR);
    vector<VALUE_TYPE> Val(nnzR);

    vector<INDEX_TYPE> RowIdx_COO(nnzR);
    vector<INDEX_TYPE> ColIdx_COO(nnzR);
    vector<VALUE_TYPE> Val_COO(nnzR);
    vector<VALUE_TYPE> Col_X_COO(nnzR);

    vector<VALUE_TYPE> X(n);
    vector<VALUE_TYPE> Y(m);
    vector<VALUE_TYPE> Y_CPU(m);
    vector<VALUE_TYPE> Y_CPU_Slice(m);
    vector<VALUE_TYPE> Y_Device(m);

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Data...";

    // 输入：filename/m/n/nnzR。
    // 输出：RowPtr/ColIdx/Val 被填成 CSR 三数组。
    Read_matrix_2_CSR(filename, 
                      m, 
                      n, 
                      nnzR, 
                      RowPtr, 
                      ColIdx, 
                      Val
                     );
#endif

    // Cuper 的 host 预处理后续按“每个非零元”做 slice/PE/HBM 重排；
    // CSR 只有行指针，没有显式 RowIdx，所以先转成 COO 三数组。
    // 把整张矩阵摊平成 COO 非零元列表
    // 输入：m/n/nnzR + CSR(RowPtr, ColIdx, Val)。
    // 输出：COO(RowIdx_COO, ColIdx_COO, Val_COO)，三者长度都是 nnzR。
    CSR_2_COO(m, 
              n, 
              nnzR, 
              RowPtr, 
              ColIdx, 
              Val, 
              RowIdx_COO, 
              ColIdx_COO, 
              Val_COO
              );
    
    cout << "  \t\tDone" << endl;

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
    
    // 输入：矩阵尺寸 m/n/nnzR、块边长 Slice_SIZE、整矩阵 COO 三数组。
    // 输出：sliceMatrix 被填成整矩阵的 SparseSlice 总容器。
    Create_SparseSlice(m, 
                       n, 
                       nnzR,
                       Slice_SIZE,
                       RowIdx_COO,
                       ColIdx_COO,
                       Val_COO,
                       sliceMatrix
                       );
    
    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Slice Size: \t\t\t" << sliceMatrix.sliceSize << endl;
    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Slice Num:  \t\t\t" << sliceMatrix.numSlices << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Prepare Matrix" << "] " << "Preparing Matrix A for FPGA...";

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

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Initialization Vector...";

    // standalone benchmark 使用确定性的输入向量 X[i] = i，方便 CPU/FPGA 对比。
    // Cuper(...) 本身只计算 y = A * x，不做 PCG 的 r/z/p 更新。
    for(INDEX_TYPE i = 0; i < n; ++i) {
        X[i] = static_cast<VALUE_TYPE>(i);
    }

    // Y 是初始输出向量；CPU reference 和 device 输出都从 0 开始。
    for(INDEX_TYPE i = 0; i < m; ++i) {
        Y[i] = 0.f;
        Y_CPU[i] = 0.f;
        Y_CPU_Slice[i] = 0.f;
        Y_Device[i] = 0.f;
    }

    // Col_X_COO 记录每个非零元对应的 X[col]，当前 main 后面没有继续使用；
    // 保留它是为了和原始 Cuper host 的调试/实验数据结构一致。
    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        Col_X_COO[i] = X[ColIdx_COO[i]];
    }

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Preparing vector X for FPGA...";
    
    // kernel 端以 float_v16 读取 X，因此 host 侧 flat float buffer 要按 16 个
    // float 对齐；同时按 1024 float 扩展，满足 mmap buffer 对齐习惯。
    INDEX_TYPE X_fpga_data_column_size = ((n + 16 - 1) / 16) * 16;
    INDEX_TYPE X_fpga_data_channel_size = ((X_fpga_data_column_size + 1023)/1024) * 1024;
    aligned_vector<VALUE_TYPE> X_fpga_data(X_fpga_data_channel_size, 0.0);
    
    for(INDEX_TYPE i = 0; i < n; ++i) {
        X_fpga_data[i] = X[i];
    }

    cout << "  \tDone" << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Preparing vector Y for FPGA...";

    // Y_out 也是 float_v16 packed 写回。这里仍用 flat float buffer 承接，
    // tapa::mmap<float>.reinterpret<float_v16>() 会在 invoke 处转换端口视图。
    INDEX_TYPE Y_fpga_data_column_size = ((m + 16 - 1) / 16) * 16;
    INDEX_TYPE Y_fpga_data_channel_size = ((Y_fpga_data_column_size + 1023)/1024) * 1024;
    aligned_vector<VALUE_TYPE> Y_fpga_data(Y_fpga_data_channel_size, 0.0);
    aligned_vector<VALUE_TYPE> Y_fpga_data_out(Y_fpga_data_channel_size, 0.0);
    
    for(INDEX_TYPE i = 0; i < m; ++i) {
        Y_fpga_data[i] = Y[i];
    }

    cout << "  \tDone" << endl;

    // CPU SpMV
    cout << "[" << setw(18) << setfill(' ') << "SpMV On CPU" << "] " << "Run SpMV On CPU...";

    // CPU baseline 直接按 CSR 计算 Y_CPU = Y + A * X，用来校验 Cuper 输出。
    auto start_cpu = std::chrono::steady_clock::now();
    SpMV_CPU_CSR(m, n, nnzR, RowPtr, ColIdx, Val, X, Y, Y_CPU);
    auto end_cpu = std::chrono::steady_clock::now();

    double time_cpu = std::chrono::duration_cast<std::chrono::nanoseconds>(end_cpu - start_cpu).count();
    time_cpu *= 1e-9;
    cout << "  \t\tDone" << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On CPU" << "] " << "Execution Time: \t\t\t" << time_cpu * 1000 << " ms" << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On CPU" << "] " << "CPU GFLOPS: \t\t\t" << 2.0 * nnzR / 1e+9 / time_cpu << endl;

    // FPGA SpMV
    // 顶层 Cuper(...) 的两个关键矩阵尺寸参数：
    //   Batch_num  = SpElement_list_ptr.size() - 1
    //   Matrix_len = SpElement_list_ptr[Batch_num]
    // Matrix_len 是每个 Matrix_data[channel] 实际读取的 512-bit beat 数。
    INDEX_TYPE SpElement_list_ptr_size = SpElement_list_ptr.size() - 1;
    INDEX_TYPE SpElement_list_ptr_max_len = SpElement_list_ptr[SpElement_list_ptr_size];
    cout << "[" << setw(18) << setfill(' ') << "SpMV On FPGA" << "] " << "Run SpMV On FPGA...";

// --- 修正后的 FPGA 运行与校验部分 ---

    // 1. 运行内核
    //
    // 参数对应 Cuper(...) ABI：
    //   SpElement_list_ptr_fpga -> batch 边界表
    //   Matrix_fpga_data        -> 16 路 HBM 矩阵数据，reinterpret 为 ap_uint<512>
    //   X_fpga_data             -> 输入向量，reinterpret 为 float_v16
    //   Y_fpga_data_out         -> 输出向量，reinterpret 为 float_v16
    //   后五个标量             -> Batch_num/Matrix_len/Row_num/Column_num/Iteration_num
    double kernel_time = tapa::invoke(Cuper, 
                                      bitstream,
                                      tapa::read_only_mmap<INDEX_TYPE>(SpElement_list_ptr_fpga),
                                      tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(Matrix_fpga_data).reinterpret<ap_uint<512>>(),
                                      tapa::read_only_mmap<float>(X_fpga_data).reinterpret<float_v16>(),
                                      tapa::write_only_mmap<float>(Y_fpga_data_out).reinterpret<float_v16>(),
                                      SpElement_list_ptr_size,
                                      SpElement_list_ptr_max_len,
                                      m,
                                      n,
                                      ITERATION_NUM
                                     );
    
    cout << " \t\tDone" << endl;
    
    // 2. 性能计算
    // tapa::invoke 返回 kernel 运行时间；原 benchmark 按 ITERATION_NUM 求单次 SpMV
    // 平均耗时，再用 2 * nnzR / time 计算 GFLOPS。
    kernel_time *= (1e-9 / ITERATION_NUM);
    double Gflops = 2.0 * nnzR / 1e+9 / kernel_time;
    cout << "[" << setw(18) << "SpMV On FPGA" << "] Execution Time: \t" << kernel_time * 1000 << " ms" << endl;
    cout << "[" << setw(18) << "SpMV On FPGA" << "] FPGA GFLOPS: \t\t" << Gflops << endl;

    // 3. 结果提取（删除所有镜像补丁逻辑）
    // kernel 写回的是 packed float_v16 视图，host buffer 本身仍是按 float 展开；
    // 因此这里直接按行号读取前 m 个 float。
    cout << "[" << setw(18) << "Verification" << "] Extracting Device Data...";
    for (INDEX_TYPE i = 0; i < m; i++) {
        Y_Device[i] = Y_fpga_data_out[i]; // 硬件已修好，此处直接赋值
    }
    cout << " Done" << endl;

    // 4. Debug 打印（验证前 16 位）
    cout << "--- Debug: First 16 elements comparison ---" << endl;
    for(int i = 0; i < 16; ++i) {
        printf("Index [%d]: CPU=%f, Device=%f\n", i, Y_CPU[i], Y_Device[i]);
    }

    // 5. 最终验证
    // Verify_correctness 做相对误差检查。这里阈值是 standalone Cuper host 的
    // FP32 SpMV 校验阈值，不是 PCG 收敛阈值。
    INDEX_TYPE error_num = Verify_correctness(m, Y_CPU, Y_Device, 1e-4); 

    if(error_num == 0)
        cout << "[" << setw(18) << "Verification" << "] Correctness Verification: \tPassed" << endl;
    else 
        cout << "[" << setw(18) << "Verification" << "] Correctness Verification: \tFailed" << endl;
    
    printf("[%18s] Error Num: %d, Error Percent: %.2f%%\n", "Verification", error_num, 100.0 * error_num / m);
}
