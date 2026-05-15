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

template <typename T>
using aligned_vector = std::vector<T, tapa::aligned_allocator<T> >;

int main(int argc, char* argv[]) {

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <matrix_file>" << endl;
        return 1;
    }

    char *filename = argv[1];

    cout << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "The Matrix name: \t\t\t" << filename << endl;

    std::string bitstream;
    if(const auto bitstream_ptr = getenv("BITFILE")) {
        bitstream = bitstream_ptr;
    } else {
        cout << "[Warning] BITFILE environment variable not set!" << endl;
    }

    INDEX_TYPE m, n, nnzR, isSymmetric;

#ifdef BINARY_READ
    vector<INDEX_TYPE> RowPtr;
    vector<INDEX_TYPE> ColIdx;
    vector<VALUE_TYPE> Val;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read binary: \t\t\t" << "ON" << endl;
    cout << "[" << setw(18) << setfill(' ') << "Read Matrix" << "] " << "Read Matrix Size...";

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

    Read_matrix_2_CSR(filename, 
                      m, 
                      n, 
                      nnzR, 
                      RowPtr, 
                      ColIdx, 
                      Val
                     );
#endif

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
    
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Slice Size: \t\t\t" << Slice_SIZE << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Batch Size: \t\t\t" << BATCH_SIZE << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "Iteration Num: \t\t\t" << ITERATION_NUM << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV Configuration" << "] " << "HBM_Channel Num: \t\t\t" << HBM_CHANNEL_NUM << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Format Conversion" << "] " << "Create Slice Format...";

    SparseSlice sliceMatrix;
    
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
    INDEX_TYPE SpElement_list_ptr_fpga_size = ((SpElement_list_ptr.size() + 15) / 16) * 16;
    INDEX_TYPE SpElement_list_ptr_fpga_channel_size = ((SpElement_list_ptr_fpga_size + 1023) / 1024) * 1024;

    SpElement_list_ptr_fpga.resize(SpElement_list_ptr_fpga_channel_size, 0);
   
    for(INDEX_TYPE i = 0; i < SpElement_list_ptr.size(); ++i) {
        SpElement_list_ptr_fpga[i] = SpElement_list_ptr[i];
    }

    vector<aligned_vector<unsigned long> > Matrix_fpga_data(HBM_CHANNEL_NUM);
    
    Create_SpElement_list_for_all_channels(SpElement_list_pes, 
                                           SpElement_list_ptr, 
                                           Matrix_fpga_data, 
                                           HBM_CHANNEL_NUM
                                          );

    cout << "  \tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Initialization Vector...";

    for(INDEX_TYPE i = 0; i < n; ++i) {
        X[i] = static_cast<VALUE_TYPE>(i);
    }

    for(INDEX_TYPE i = 0; i < m; ++i) {
        Y[i] = 0.f;
        Y_CPU[i] = 0.f;
        Y_CPU_Slice[i] = 0.f;
        Y_Device[i] = 0.f;
    }

    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        Col_X_COO[i] = X[ColIdx_COO[i]];
    }

    cout << "  \t\tDone" << endl;

    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Preparing vector X for FPGA...";
    
    INDEX_TYPE X_fpga_data_column_size = ((n + 16 - 1) / 16) * 16;
    INDEX_TYPE X_fpga_data_channel_size = ((X_fpga_data_column_size + 1023)/1024) * 1024;
    aligned_vector<VALUE_TYPE> X_fpga_data(X_fpga_data_channel_size, 0.0);
    
    for(INDEX_TYPE i = 0; i < n; ++i) {
        X_fpga_data[i] = X[i];
    }

    cout << "  \tDone" << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Prepare Vector" << "] " << "Preparing vector Y for FPGA...";

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

    auto start_cpu = std::chrono::steady_clock::now();
    SpMV_CPU_CSR(m, n, nnzR, RowPtr, ColIdx, Val, X, Y, Y_CPU);
    auto end_cpu = std::chrono::steady_clock::now();

    double time_cpu = std::chrono::duration_cast<std::chrono::nanoseconds>(end_cpu - start_cpu).count();
    time_cpu *= 1e-9;
    cout << "  \t\tDone" << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On CPU" << "] " << "Execution Time: \t\t\t" << time_cpu * 1000 << " ms" << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On CPU" << "] " << "CPU GFLOPS: \t\t\t" << 2.0 * nnzR / 1e+9 / time_cpu << endl;

    // FPGA SpMV
    INDEX_TYPE SpElement_list_ptr_size = SpElement_list_ptr.size() - 1;
    INDEX_TYPE SpElement_list_ptr_max_len = SpElement_list_ptr[SpElement_list_ptr_size];
    cout << "[" << setw(18) << setfill(' ') << "SpMV On FPGA" << "] " << "Run SpMV On FPGA...";

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
    cout << "  \t\tDone" << endl;
    
    kernel_time *= (1e-9 / ITERATION_NUM);
    double Gflops = 2.0 * nnzR / 1e+9 / kernel_time;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On FPGA" << "] " << "Execution Time: \t\t\t" << kernel_time * 1000 << " ms" << endl;
    cout << "[" << setw(18) << setfill(' ') << "SpMV On FPGA" << "] " << "FPGA GFLOPS: \t\t\t" << Gflops << endl;
cout << "--- Debug: First 16 elements comparison ---" << endl;
for(int i = 0; i < 16; ++i) {
    printf("Index [%d]: CPU(Row %d)=%f, Device(From FPGA)=%f\n", 
            i, i, Y_CPU[i], Y_fpga_data_out[i]);
}
    // Verification
    cout << "[" << setw(18) << setfill(' ') << "Verification" << "] " << "Verify the correctness of Y...";
    
    for(INDEX_TYPE i = 0; i < m; ++i) {
        for (INDEX_TYPE i = 0; i < m; i++) {
    INDEX_TYPE pack_base = (i / 16) * 16; // 找到当前对应的 v16 包起始位置
    INDEX_TYPE inner_idx = i % 16;        // 在 v16 包内的偏移

    if (inner_idx % 2 == 0) {
        // 偶数行（Ping）：位置是正确的
        Y_Device[i] = Y_fpga_data_out[i];
    } else {
        // 奇数行（Pong）：应用我们发现的循环偏移 (inner_idx + 8) % 16
        // 映射关系：1->9, 3->11, 5->13, 7->15, 9->1, 11->3, 13->5, 15->7
        INDEX_TYPE corrected_idx = pack_base + ((inner_idx + 8) % 16);
        Y_Device[i] = Y_fpga_data_out[corrected_idx];
    }
}
    }

    INDEX_TYPE error_num = Verify_correctness(m, Y_CPU, Y_Device, THRESHOLD);

    cout << "  \tDone" << endl;

    VALUE_TYPE diffpercent = 100.0 * (VALUE_TYPE)error_num / m;
    bool ispass = (error_num == 0);

    if(ispass)
        cout << "[" << setw(18) << setfill(' ') << "Verification" << "] " << "Correctness Verification \t\t" << "Passed" << endl;
    else 
        cout << "[" << setw(18) << setfill(' ') << "Verification" << "] " << "Correctness Verification \t\t" << "Failed" << endl;
    
    cout << "[" << setw(18) << setfill(' ') << "Verification" << "] ";
    printf("Error Num: \t\t\t%d\n", error_num);
    cout << "[" << setw(18) << setfill(' ') << "Verification" << "] ";
    printf("Error Percent: \t\t\t%.2f%%\n\n", diffpercent);

    return 0;
}