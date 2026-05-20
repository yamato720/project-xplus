#ifndef PROJECT_XPLUS_CG_COMMON_HPP
#define PROJECT_XPLUS_CG_COMMON_HPP

#include <cstddef>

namespace project_xplus::cgsolver {

using data_t = double;
using index_t = int;

static constexpr int kMaxN = 1024;
static constexpr data_t kBreakdownEps = 1.0e-30;
// 滑动窗口 SpMV 每次缓存到片上的 x 向量长度。
//
// 这个值只限制单个窗口占用的 BRAM，不再限制完整矩阵/向量维度。
// 当前固定为 512，原因是：
//   1. 512 个 double 约 4 KiB，适合放进 BRAM
//   2. 512 是 4x4 block size 的整数倍，任意 block column 都不会跨窗口
//   3. 对 nasa2910 这类 n=2910 的矩阵只需要 6 个窗口，便于验证
static constexpr int kSpmvWindowSize = 512;
// 分块 SpMV 的块大小。当前固定为 4x4：
//   一个 block row 覆盖 4 行
//   一个 block column 覆盖 4 列
// 这个值同时被 host 端 CSR->block 转换和 kernel 端 block SpMV 使用，
// 两边必须保持一致。
static constexpr int kSpmvBlockSize = 4;
static constexpr int kSpmvBlockEntries = kSpmvBlockSize * kSpmvBlockSize;
// Row-tile SpMV 每次在片上保留多少个 4-row block row 的 y/ap 部分和。
//
// 当前为 2048 个 block row，也就是 8192 个 double 输出元素，约 64 KiB。
// 这个 tile 在 kernel 内跨所有 x-window 累加，最后一次性写回 HBM。
// 取值越大，重复加载 x_window 的次数越少，但片上 BRAM 占用越高。
// 2048 可以让 nasa2910/nasa4704 这类中等矩阵在一次 SpMV 内只用一个 row tile，
// 同时对 pwt/nasasrb/thermal2 这类更大矩阵减少一半以上的 x_window 重载次数。
static constexpr int kSpmvRowTileBlockRows = 2048;

struct SpmvBlock {
    // 4x4 block/bitmap SpMV 的一个非零块。
    //
    // values 按 block 内 row-major 位置压缩存储，只保存 mask=1 的值。
    // 举例：如果 mask 的 bit0、bit5 为 1，那么：
    //   values[0] 对应 block 内 (0,0)
    //   values[1] 对应 block 内 (1,1)
    //
    // indices[2:3] 保存 16-bit occupancy mask：
    //   bit(local_r * 4 + local_c) == 1 表示该位置有非零值。
    // indices 其余位置暂时保留，便于后续扩展更复杂的块内编码。
    data_t values[kSpmvBlockEntries];
    unsigned char indices[kSpmvBlockEntries];
};

struct SolverConfig {
    data_t tau = 1.0e-10;
    int max_iters = 0;
};

struct RunSummary {
    int n = 0;
    int nnz = 0;
    int iterations = 0;
    data_t final_rr = 0.0;
    data_t residual_l2 = 0.0;
    bool converged = false;
};

}  // namespace project_xplus::cgsolver

#endif
