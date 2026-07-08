#ifndef CALLIPEPLA_H
#define CALLIPEPLA_H

#include <ap_int.h>
#include <tapa.h>

// 稀疏矩阵按 16 路 HBM/PE 分片并行读取。
constexpr int NUM_CH_SPARSE = 16;

// X 向量在 PE 内按窗口缓存；分区因子控制 BRAM bank 数和并行读口压力。
constexpr int X_PARTITION_FACTOR = 4; // BRAMs = 512 / 16 / 2 = 16 -> factor = 16 / (64 / 16)
constexpr int WINDOW_SIZE = X_PARTITION_FACTOR * 1024;
// 对累加数组显式声明 load/store 相关距离，帮助 HLS 保持目标 II。
constexpr int DEP_DIST_LOAD_STORE = 7;
// 每个 PE 的本地 Y 累加缓冲深度，映射到 URAM。
constexpr int URAM_DEPTH = 3 * 4096;
// ch16: 3 * 4096 * 16 * 8 = 1,572,864

// 外部向量按 8 个 double 打包成 512-bit 传输。
using double_v8 = tapa::vec_t<double, 8>;

// 顶层 TAPA kernel：执行 Jacobi 预条件 CG，结果残差写入 vec_res。
void Callipepla(tapa::mmap<int> edge_list_ptr,
                tapa::mmaps<ap_uint<512>, NUM_CH_SPARSE> edge_list_ch,
                tapa::mmaps<double_v8, 2> vec_x,
                tapa::mmaps<double_v8, 2> vec_p,
                tapa::mmap<double_v8> vec_Ap,
                tapa::mmaps<double_v8, 2> vec_r,
                tapa::mmap<double_v8> vec_digA,
                tapa::mmap<double> vec_res,
                const int NUM_ITE, const int NUM_A_LEN, const int M,
                const int rp_time,
                const double th_termination
                );

#endif  // CALLIPEPLA_H
