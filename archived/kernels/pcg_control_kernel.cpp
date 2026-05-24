#include "../../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
using Block = project_xplus::cgsolver::SpmvBlock;
constexpr data_t kBreakdownEps = project_xplus::cgsolver::kBreakdownEps;
constexpr int kBlockSize = project_xplus::cgsolver::kSpmvBlockSize;
constexpr int kBlockEntries = project_xplus::cgsolver::kSpmvBlockEntries;
constexpr int kWindowSize = project_xplus::cgsolver::kSpmvWindowSize;
constexpr int kRowTileBlockRows = project_xplus::cgsolver::kSpmvRowTileBlockRows;
constexpr int kRowTileEntries = kRowTileBlockRows * kBlockSize;
// 滑动窗口大小必须是 block size 的整数倍。这样一个 4x4 block column
// 不会横跨两个 x 窗口，kernel 里只需要判断 block column 属于哪个窗口，
// 不需要把同一个 block 拆成两半处理。
static_assert(kWindowSize % kBlockSize == 0,
              "SpMV window size must be a multiple of the block size");
static_assert(kRowTileBlockRows > 0, "SpMV row tile block rows must be positive");

enum StatusCode {
    // 这几个状态码会写回 status[0]，host 用它判断本次运行是否成功。
    // 这里不用 enum class 是为了保持 HLS 侧简单整数编码，方便 host 直接读。
    kStatusConverged = 0,
    kStatusMaxIter = 1,
    kStatusBreakdown = 2,
};

inline data_t abs_value(const data_t value) {
    // 避免额外引入 <cmath> 和库函数映射；HLS 中这个三目表达式会综合成
    // 很小的比较/选择逻辑。
    return value < 0.0 ? -value : value;
}

inline bool invalid_scalar(const data_t value) {
    // NaN 是唯一一个和自己比较不相等的浮点值。这里用 value != value
    // 检测 alpha/beta/rz/rr/pAp 是否已经数值失效。
    return value != value;
}

void load_x_window(const data_t* x,
                   data_t x_window[kWindowSize],
                   const int window_begin,
                   const int n) {
// 从 HBM 上的完整 x/p 向量中取一段固定长度窗口到片上 BRAM。
//
// x 形参在初始化 SpMV 时指向 x_bo，在主循环 SpMV 时指向 p_bo。
// 这个函数不关心数学含义，只负责把 [window_begin, window_begin+kWindowSize)
// 这段向量变成片上随机可读的 x_window。
//
// 最后一个窗口可能越界，越界部分补 0；这样 block 内统一读 x_window，
// 不需要为最后一个窗口写另一套循环。
// HLS pragma: INLINE off 保留窗口加载函数的任务边界，使调用它的
// DATAFLOW 区域可以把“load x window”和“compute window”并行调度。
#pragma HLS INLINE off
load_x_window_loop:
    for (int offset = 0; offset < kWindowSize; ++offset) {
// HLS pragma: PIPELINE II=1 让窗口搬运循环尽量每周期读/写一个元素。
// 对最后一个窗口的越界判断会变成简单比较和选择逻辑。
#pragma HLS PIPELINE II = 1
        const int global_col = window_begin + offset;
        x_window[offset] = (global_col < n) ? x[global_col] : 0.0;
    }
}

void zero_y_tile(data_t y_tile[kRowTileEntries], const int tile_entries) {
// 把当前 row tile 的 y/ap 部分和清零。
//
// 旧版本按 x-window 写回 HBM：第 0 个窗口写第一份部分和，后续窗口再读回
// HBM 上的旧部分和继续累加。现在改成 row tile 后，部分和在 BRAM 中跨
// 所有 x-window 保留，所以每个 row tile 开始时只需要清一次片上 y_tile。
//
// HLS pragma: INLINE off 保留清零任务边界。这里不放进 DATAFLOW，是因为
// 后面的窗口累加必须等清零完成；保持单独函数有利于阅读综合报告中的阶段。
#pragma HLS INLINE off
zero_y_tile_loop:
    for (int offset = 0; offset < tile_entries; ++offset) {
// HLS pragma: PIPELINE II=1 尝试让 BRAM 清零每周期写一个 double。
// tile_entries 最大为 kRowTileEntries，最后一个 row tile 可能更短。
#pragma HLS PIPELINE II = 1
        y_tile[offset] = 0.0;
    }
}

data_t write_y_tile(data_t* y,
                    const data_t* x,
                    const data_t y_tile[kRowTileEntries],
                    const int n,
                    const int row_tile_begin_br,
                    const int tile_entries,
                    const bool accumulate_x_dot_y) {
// 把当前 row tile 的最终 SpMV 输出写回 HBM。
//
// 数学上，这里写回的是：
//   y[row_tile] = sum_w A[row_tile, W_w] * x[W_w]
//
// 与旧版本最大的区别是：每个输出元素每次 SpMV 只写 HBM 一次，不再随
// x-window 数量重复 read/modify/write。
// 如果 accumulate_x_dot_y 为 true，这一趟写回同时归约 x^T y。PCG 主循环里
// x 实际指向 p，y 实际指向 ap，因此这里直接得到 pAp，避免 SpMV 写完 ap 后
// 再单独从 HBM 读一遍 p/ap 做 dot。
// HLS pragma: INLINE off 保留写回任务边界，便于和 zero/accumulate 阶段区分。
#pragma HLS INLINE off
    const int row_tile_begin = row_tile_begin_br * kBlockSize;
    data_t dot = 0.0;

write_y_tile_loop:
    for (int offset = 0; offset < tile_entries; ++offset) {
// HLS pragma: PIPELINE II=1 尝试让 row tile 写回循环连续发起 HBM 写请求。
// 最后一个 block row 可能只有 1~3 个有效行，所以仍保留 global_r < n 判断。
#pragma HLS PIPELINE II = 1
        const int global_r = row_tile_begin + offset;
        if (global_r < n) {
            const data_t y_value = y_tile[offset];
            y[global_r] = y_value;
            if (accumulate_x_dot_y) {
                dot += x[global_r] * y_value;
            }
        }
    }

    return dot;
}

void accumulate_window_tile(const index_t* a_win_row_ptr,
                            const index_t* a_win_col_idx,
                            const Block* a_win_blocks,
                            const data_t x_window[kWindowSize],
                            data_t y_tile[kRowTileEntries],
                            const int n,
                            const int window_id,
                            const int window_begin,
                            const int row_tile_begin_br,
                            const int tile_block_rows) {
// 计算“一个列窗口 x_window”对“一个行 tile y_tile”的贡献。
//
// 数学上等价于：
//   y_tile += A[row_tile, window_begin:window_end) * x_window
//
// 这里的 A 已经由 host 重排成 window-major block row 格式：
//   a_win_row_ptr[window_id][br] ~ a_win_row_ptr[window_id][br+1]
// 直接给出当前 column window、当前 block row 的非零 4x4 block 子流。
//
// row tile 的意义：
//   1. y_tile 放在片上 BRAM
//   2. 同一个 row tile 会依次扫过所有 x-window
//   3. 各 window 的部分和直接累到 y_tile
//   4. 全部 window 累完以后才写回 HBM
//
// 因此这个函数不再访问 HBM 上的 y/ap，也不需要 clear_output 参数。
// HLS pragma: INLINE off 保留窗口累加函数边界，使 DATAFLOW helper 可以把
// “当前窗口计算”和“下一窗口预取”分成两个任务。
#pragma HLS INLINE off
    const int num_block_rows = (n + kBlockSize - 1) / kBlockSize;
    const int row_ptr_base = window_id * (num_block_rows + 1);

row_tile_block_rows:
    for (int local_br = 0; local_br < tile_block_rows; ++local_br) {
        const int br = row_tile_begin_br + local_br;
        const int tile_row_base = local_br * kBlockSize;

        // 当前 block row 对应 y_tile 中连续 4 行。旧实现每处理一个
        // 非零元素都直接 y_tile[tile_row] += ...，这会让 BRAM 成为
        // read-modify-write 归约瓶颈。这里先把 4 个输出部分和读入
        // 寄存器 y_accum，处理完整个 block row/window 后再写回一次。
        //
        // 数学含义不变：
        //   y_accum[r] = y_tile[base+r]
        //   y_accum[r] += sum_c A_block(r,c) * x_window[c]
        //   y_tile[base+r] = y_accum[r]
        //
        // 这样 y_tile 的 BRAM 访问次数从“每个非零值一次读改写”降到
        // “每个 block row 4 次读 + 4 次写”，中间高频累加都在寄存器里完成。
        data_t y_accum[kBlockSize];
// HLS pragma: ARRAY_PARTITION complete 把 4 个累加器完全拆成独立寄存器。
// block 内 4 行可以被独立更新，不再共享同一个小数组端口。
#pragma HLS ARRAY_PARTITION variable = y_accum complete dim = 1

    read_y_accum:
        for (int local_r = 0; local_r < kBlockSize; ++local_r) {
// HLS pragma: UNROLL 完全展开 4 次读，让 HLS 生成 4 个标量寄存器赋值。
// y_tile 是双端口 BRAM，综合器会按端口能力调度这些读；读完后后续累加
// 不再访问 y_tile。
#pragma HLS UNROLL
            y_accum[local_r] = y_tile[tile_row_base + local_r];
        }

        // 当前 window 的第 br 个 block row 在 a_win_blocks 里的连续范围。
        // 如果这个范围为空，说明 A[br, current_window] 没有非零 block，
        // y_tile 里对应 4 行保持已有部分和即可。
        const int block_start = a_win_row_ptr[row_ptr_base + br];
        const int block_end = a_win_row_ptr[row_ptr_base + br + 1];

    blocks_in_tile_row:
        for (int bi = block_start; bi < block_end; ++bi) {
// HLS pragma: PIPELINE II=1 尝试让当前 row/window 的 block 流连续处理。
// y_tile 不再位于这个流水循环的读改写路径上；同一 block row 内的归约
// 依赖集中在 y_accum[4] 寄存器里，期望降低旧版 blocks_in_tile_row 的 II。
// 浮点加法本身仍有真实依赖，所以最终 II 还取决于 HLS 对 double adder
// latency 和寄存器归约链的调度结果。
#pragma HLS PIPELINE II = 1
            // a_win_* 已经按 window 分组，bi 范围内的 block 全部属于
            // 当前 x_window，不需要再比较 block column 是否落在窗口内。
            const int bc = a_win_col_idx[bi];
            // block 按值读到局部变量。HLS 通常会把 mask 和 compact values
            // 临时放进寄存器/局部连线，避免内层循环重复访问 HBM。
            const Block block = a_win_blocks[bi];
            const unsigned short mask =
                static_cast<unsigned short>(block.indices[2]) |
                static_cast<unsigned short>(static_cast<unsigned short>(block.indices[3]) << 8);

            int value_index = 0;
        block_entries:
            for (int pos = 0; pos < kBlockEntries; ++pos) {
                if ((mask >> pos) & 1u) {
                    // mask 的 bit 位置按 row-major 编码：
                    //   pos = local_r * 4 + local_c
                    // values[] 是 compact 存储，只有 mask=1 的位置才消费
                    // 下一个 value_index。
                    const int local_r = pos / kBlockSize;
                    const int local_c = pos % kBlockSize;
                    const int global_c = bc * kBlockSize + local_c;
                    const int window_offset = global_c - window_begin;
                    // 正常情况下，host 重排保证当前 block column 属于当前
                    // window。这里仍保留边界保护，覆盖最后一个 block column
                    // 不满 4 列，以及防御异常 metadata 的情况。
                    const data_t x_value =
                        (global_c < n && window_offset >= 0 && window_offset < kWindowSize)
                            ? x_window[window_offset]
                            : 0.0;
                    y_accum[local_r] += block.values[value_index] * x_value;
                    value_index++;
                }
            }
        }

    write_y_accum:
        for (int local_r = 0; local_r < kBlockSize; ++local_r) {
// HLS pragma: UNROLL 完全展开 4 次写回。这里是每个 block row/window 的
// 唯一 y_tile 写入点，避免内层非零值循环频繁打 BRAM。
#pragma HLS UNROLL
            y_tile[tile_row_base + local_r] = y_accum[local_r];
        }
    }
}

void process_ping_tile_and_prefetch_pong(const index_t* a_win_row_ptr,
                                         const index_t* a_win_col_idx,
                                         const Block* a_win_blocks,
                                         const data_t* x,
                                         const data_t x_window_ping[kWindowSize],
                                         data_t x_window_pong[kWindowSize],
                                         data_t y_tile[kRowTileEntries],
                                         const int n,
                                         const int window_id,
                                         const int window_begin,
                                         const int row_tile_begin_br,
                                         const int tile_block_rows,
                                         const int next_window_begin) {
// HLS pragma: INLINE off 保留这个 ping 版本 helper 的函数边界。
// DATAFLOW 对函数调用边界更敏感，保留下来有利于形成“计算+预取”的任务图。
#pragma HLS INLINE off
    // DATAFLOW 区域里不能放条件执行。这个 helper 固定做两件事：
    //   1. 用 ping buffer 计算当前 window 对当前 row tile 的贡献
    //   2. 预取下一 window 的 x 到 pong buffer
    // y_tile 只被计算任务读写；预取任务只写 pong buffer，两个任务无数据冲突。
// HLS pragma: DATAFLOW 启用任务级流水，让下面两个函数调用尽可能重叠：
// accumulate_window_tile 读取当前 ping 窗口做计算，同时 load_x_window
// 把下一窗口预取到 pong。HLS 可能在任务间插入隐式 PIPO/FIFO。
#pragma HLS DATAFLOW
    accumulate_window_tile(a_win_row_ptr,
                           a_win_col_idx,
                           a_win_blocks,
                           x_window_ping,
                           y_tile,
                           n,
                           window_id,
                           window_begin,
                           row_tile_begin_br,
                           tile_block_rows);
    load_x_window(x, x_window_pong, next_window_begin, n);
}

void process_pong_tile_and_prefetch_ping(const index_t* a_win_row_ptr,
                                         const index_t* a_win_col_idx,
                                         const Block* a_win_blocks,
                                         const data_t* x,
                                         data_t x_window_ping[kWindowSize],
                                         const data_t x_window_pong[kWindowSize],
                                         data_t y_tile[kRowTileEntries],
                                         const int n,
                                         const int window_id,
                                         const int window_begin,
                                         const int row_tile_begin_br,
                                         const int tile_block_rows,
                                         const int next_window_begin) {
// HLS pragma: INLINE off 保留这个 pong 版本 helper 的函数边界。
// 这样它内部的 DATAFLOW 可以把“读 pong 计算”和“写 ping 预取”拆成任务。
#pragma HLS INLINE off
    // 和上面的 helper 对称：当前窗口读 pong，同时把下一窗口预取到 ping。
// HLS pragma: DATAFLOW 启用任务级流水。这里和 ping helper 对称：
// 当前计算使用 pong buffer，下一窗口加载写入 ping buffer，两个任务尝试并行。
#pragma HLS DATAFLOW
    accumulate_window_tile(a_win_row_ptr,
                           a_win_col_idx,
                           a_win_blocks,
                           x_window_pong,
                           y_tile,
                           n,
                           window_id,
                           window_begin,
                           row_tile_begin_br,
                           tile_block_rows);
    load_x_window(x, x_window_ping, next_window_begin, n);
}

data_t spmv_blocked_windowed(const index_t* a_win_row_ptr,
                             const index_t* a_win_col_idx,
                             const Block* a_win_blocks,
                             const data_t* x,
                             data_t* y,
                             const int n,
                             const bool accumulate_x_dot_y) {
// 当前 pcg_control_kernel 内部使用 4x4 block/bitmap + 二维分块 SpMV。
//
// Host 会先把 CSR 矩阵转换成块格式：
//   a_win_row_ptr[w][br] ~ a_win_row_ptr[w][br+1] 给出 window w、block row br
//     对应的非零块区间
//   a_win_col_idx[bi] 给出第 bi 个非零块的 block column
//   a_win_blocks[bi].indices[2:3] 保存 16-bit occupancy mask
//   a_win_blocks[bi].values[] 按 block 内 row-major 顺序紧凑保存 mask=1 的值
//
// 一个 block 覆盖 A 的 4x4 子矩阵。block 内位置编码为：
//   pos = local_r * 4 + local_c
//
// x 和 y 都是 HBM 上的向量 BO。和早期整条 x_local[kMaxN] 缓存不同，
// 这里每次只把 x 的一个固定窗口加载到片上 BRAM：
//   x_window[0:kWindowSize) = x[window_begin:window_end)
//
// 这样 n 不再受完整向量片上数组限制，同时 block 内访问 x 时命中 BRAM。
// A 也已经按 x-window 分组，所以每个 x 窗口只顺序读当前窗口真正包含的
// block 子流，不再反复扫描不属于当前窗口的 block metadata。
//
// 另外，y/ap 不再每处理一个 x-window 就读写 HBM。这里增加 row tile：
//   y_tile[0:kRowTileEntries) = 当前一批 block row 的片上部分和
//
// 计算顺序变成：
//   for row_tile:
//     清零片上 y_tile
//     for x_window:
//       读取 A[row_tile, x_window] 和 x_window
//       累加到 y_tile
//     y_tile 一次性写回 HBM
//
// 对 nasa2910，num_block_rows=ceil(2910/4)=728，kRowTileBlockRows=2048，
// 因此每次 SpMV 只有一个 row tile。这样 y/ap 的 HBM 写回次数按 row tile
// 发生，而不是按 window 数量重复发生。
//
// 为了让 HLS 有机会做窗口级 task pipeline，这里使用 ping-pong 双缓冲：
//   - buffer A 正在被 compute 读取时
//   - buffer B 可以同时预取下一个 x 窗口
// 下一轮交换两个 buffer。这个 dataflow 只覆盖 SpMV 的窗口级 load/compute，
// 不能跨 PCG 的 alpha/beta 标量依赖。
//
// y 在不同阶段复用同一个 ap BO：
//   初始化阶段: y 表示 ax0 = A * x0
//   主循环阶段: y 表示 ap  = A * p
//
// 主循环阶段还可以在 row tile 写回时顺手计算 x^T y，也就是 p^T ap。
// 这样保留 ap 给后续 r 更新，但省掉原来紧跟着 SpMV 的独立 dot_loop。
//
// 注意：这是单 kernel 内的片上缓存窗口，不是多次 kernel launch。
// 完整 PCG 仍然在一次 pcg_control_kernel launch 内完成。
    const int num_windows = (n + kWindowSize - 1) / kWindowSize;
    const int num_block_rows = (n + kBlockSize - 1) / kBlockSize;
    data_t dot = 0.0;

    // 这两个数组是滑动窗口的 ping-pong 片上缓存。BIND_STORAGE 明确要求
    // 综合成 BRAM，避免 HLS 把窗口拆成大量寄存器。complete partition
    // 不能用于这里，否则资源会随 kWindowSize 线性爆炸。
    data_t x_window_ping[kWindowSize];
    data_t x_window_pong[kWindowSize];
// HLS pragma: BIND_STORAGE 指定 x_window_ping 使用双端口 BRAM。
// ram_2p 提供两个访问端口，impl=bram 避免窗口数组被综合成大量寄存器或 LUTRAM。
#pragma HLS BIND_STORAGE variable = x_window_ping type = ram_2p impl = bram
// HLS pragma: BIND_STORAGE 指定 x_window_pong 使用双端口 BRAM。
// ping/pong 两个窗口分开成两块片上存储，便于一个被计算读、另一个被预取写。
#pragma HLS BIND_STORAGE variable = x_window_pong type = ram_2p impl = bram

    // y_tile 是当前 row tile 的片上输出部分和。BIND_STORAGE 明确要求使用
    // BRAM，避免 HLS 为 2048 个 double 生成大量寄存器。
    //
    // 这里没有 ARRAY_PARTITION complete：y_tile 是大数组，完全拆分会让资源
    // 线性爆炸；保留 BRAM 形态更符合“行块缓存”的目标。
    data_t y_tile[kRowTileEntries];
// HLS pragma: BIND_STORAGE 指定 y_tile 使用双端口 BRAM。
// 计算阶段对 y_tile 做 read-modify-write；写回阶段再顺序读出写 HBM。
#pragma HLS BIND_STORAGE variable = y_tile type = ram_2p impl = bram

row_tiles:
    for (int row_tile_begin_br = 0; row_tile_begin_br < num_block_rows;
         row_tile_begin_br += kRowTileBlockRows) {
        const int remaining_block_rows = num_block_rows - row_tile_begin_br;
        const int tile_block_rows =
            (remaining_block_rows < kRowTileBlockRows) ? remaining_block_rows : kRowTileBlockRows;
        const int tile_entries = tile_block_rows * kBlockSize;

        // 每个 row tile 从 0 开始累加所有 x-window 的贡献。因为 y_tile 在
        // 片上跨 window 保留，后续 window 不再从 HBM 读回旧 y 部分和。
        zero_y_tile(y_tile, tile_entries);

        // 先预取第 0 个窗口。进入下面循环后，每轮都是“计算当前窗口，
        // 同时预取下一窗口”。如果只有一个窗口，下面的 for 不执行，
        // 尾窗口逻辑会直接使用这个 ping buffer 完成计算。
        load_x_window(x, x_window_ping, 0, n);

    x_windows:
        for (int window_id = 0; window_id + 1 < num_windows; ++window_id) {
            const int window_begin = window_id * kWindowSize;
            const int next_window_begin = (window_id + 1) * kWindowSize;
            const bool use_ping_for_compute = ((window_id & 1) == 0);

            // 这里不直接写 DATAFLOW 区域，而是调用两个无条件 helper。
            // 原因是 Vitis HLS 2022.2 不支持在 DATAFLOW 区域里出现条件执行。
            // 每个 helper 内部固定把两个互不冲突的任务并起来：
            //   1. 用当前 buffer 计算当前 window 对当前 row tile 的贡献
            //   2. 用另一个 buffer 预取下一 window 的 x
            //
            // 这层 dataflow 只隐藏 x_window 预取延迟；row tile 之间仍顺序执行，
            // 因为 y_tile 的清零、累加、写回存在明确先后关系。
            if (use_ping_for_compute) {
                // 偶数窗口读 ping，下一窗口预取到 pong。
                process_ping_tile_and_prefetch_pong(a_win_row_ptr,
                                                    a_win_col_idx,
                                                    a_win_blocks,
                                                    x,
                                                    x_window_ping,
                                                    x_window_pong,
                                                    y_tile,
                                                    n,
                                                    window_id,
                                                    window_begin,
                                                    row_tile_begin_br,
                                                    tile_block_rows,
                                                    next_window_begin);
            } else {
                // 奇数窗口读 pong，下一窗口预取到 ping。
                process_pong_tile_and_prefetch_ping(a_win_row_ptr,
                                                    a_win_col_idx,
                                                    a_win_blocks,
                                                    x,
                                                    x_window_ping,
                                                    x_window_pong,
                                                    y_tile,
                                                    n,
                                                    window_id,
                                                    window_begin,
                                                    row_tile_begin_br,
                                                    tile_block_rows,
                                                    next_window_begin);
            }
        }

        // 最后一个窗口没有“下一窗口”可预取，只做计算。这个尾窗口不能放进
        // 上面的 dataflow helper，否则就会重新引入 dataflow 内的条件执行。
        const int last_window_id = num_windows - 1;
        const int last_window_begin = last_window_id * kWindowSize;
        if ((last_window_id & 1) == 0) {
            // 如果最后一个窗口编号是偶数，最后一次预取落在 ping。
            accumulate_window_tile(a_win_row_ptr,
                                   a_win_col_idx,
                                   a_win_blocks,
                                   x_window_ping,
                                   y_tile,
                                   n,
                                   last_window_id,
                                   last_window_begin,
                                   row_tile_begin_br,
                                   tile_block_rows);
        } else {
            // 如果最后一个窗口编号是奇数，最后一次预取落在 pong。
            accumulate_window_tile(a_win_row_ptr,
                                   a_win_col_idx,
                                   a_win_blocks,
                                   x_window_pong,
                                   y_tile,
                                   n,
                                   last_window_id,
                                   last_window_begin,
                                   row_tile_begin_br,
                                   tile_block_rows);
        }

        // 当前 row tile 已经收集完所有 x-window 的贡献，现在一次性写回 HBM。
        // PCG 主循环里同时把这个 row tile 对 pAp 的贡献归约到 dot。
        dot += write_y_tile(y,
                            x,
                            y_tile,
                            n,
                            row_tile_begin_br,
                            tile_entries,
                            accumulate_x_dot_y);
    }

    return dot;
}

}  // namespace

extern "C" {

void pcg_control_kernel(const project_xplus::cgsolver::index_t* a_win_row_ptr,
                        const project_xplus::cgsolver::index_t* a_win_col_idx,
                        const project_xplus::cgsolver::SpmvBlock* a_win_blocks,
                        const project_xplus::cgsolver::data_t* b,
                        const project_xplus::cgsolver::data_t* m_inv,
                        project_xplus::cgsolver::data_t* x,
                        project_xplus::cgsolver::data_t* r,
                        project_xplus::cgsolver::data_t* z,
                        project_xplus::cgsolver::data_t* p,
                        project_xplus::cgsolver::data_t* ap,
                        project_xplus::cgsolver::data_t* metrics,
                        int* status,
                        project_xplus::cgsolver::data_t tau,
                        int max_iters,
                        int n) {
// 单顶层 Jacobi-PCG kernel。
//
// 这个 kernel 的设计目标是把原来 host 每轮做的控制搬到 FPGA 内：
//   1. host 只负责准备 BO，然后 launch 这一个 kernel
//   2. x/r/z/p/ap 等 PCG 向量状态常驻 HBM
//   3. kernel 内用 4x4 block/bitmap SpMV 得到 ax0 = A*x0
//   4. kernel 内生成 r0 / z0 / p0 / rz0 / rr0
//   5. kernel 内循环执行 SpMV、dot、alpha、x/r/z 更新、beta、p 更新
//   6. 收敛、max_iter 和 breakdown 都在 kernel 内决定
//
// Host 只需要 launch 一次 kernel，最后取回 x、metrics 和 status。和早期
// 版本不同，这里不再声明 x_local/r_local/z_local/p_local/ap_local[kMaxN]
// 这种整条向量 BRAM 缓存，因此更适合向大规模数据扩展。
//
// metrics 的布局：
//   metrics[0] = 最终 rz = r^T z
//   metrics[1] = 最终 rr = r^T r
//   metrics[2] = 最后一轮 pAp = p^T A p
//   metrics[3] = 最后一轮 alpha
//
// status 的布局：
//   status[0] = 0 converged, 1 max_iter, 2 breakdown/非法输入
//   status[1] = 实际完成的 PCG 迭代轮数
//
// 注意：顶层这里没有使用 #pragma HLS DATAFLOW。原因是 PCG 主循环存在
// 严格的标量依赖：
//   pAp -> alpha -> update_xrz -> rz_new/rr_new -> beta -> update_p
// 这些依赖必须按顺序完成。当前只在 SpMV 窗口内部做局部 dataflow，
// 顶层仍是一个控制状态机顺序推进各个数学阶段。
// HLS pragma: 下面这一组 s_axilite 把每个 kernel 参数映射到 AXI-Lite
// control bundle。host 通过 xrt::run::set_arg 写这些控制寄存器/指针，
// kernel 的 start/done/return 也走同一个 control 接口。
#pragma HLS INTERFACE s_axilite port = a_win_row_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = a_win_col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = a_win_blocks bundle = control
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = m_inv bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = r bundle = control
#pragma HLS INTERFACE s_axilite port = z bundle = control
#pragma HLS INTERFACE s_axilite port = p bundle = control
#pragma HLS INTERFACE s_axilite port = ap bundle = control
#pragma HLS INTERFACE s_axilite port = metrics bundle = control
#pragma HLS INTERFACE s_axilite port = status bundle = control
#pragma HLS INTERFACE s_axilite port = tau bundle = control
#pragma HLS INTERFACE s_axilite port = max_iters bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

// HLS pragma: 下面这一组 m_axi 为指针参数生成 AXI master 访存端口。
// offset=slave 表示指针基地址仍由 AXI-Lite 控制寄存器传入；bundle 名
// 决定逻辑内存端口分组，link 阶段再由 connectivity 配置绑定到 HBM bank。
#pragma HLS INTERFACE m_axi port = a_win_row_ptr offset = slave bundle = gmem_row
#pragma HLS INTERFACE m_axi port = a_win_col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = a_win_blocks offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem_b
#pragma HLS INTERFACE m_axi port = m_inv offset = slave bundle = gmem_minv
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = r offset = slave bundle = gmem_r
#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p
#pragma HLS INTERFACE m_axi port = ap offset = slave bundle = gmem_ap
#pragma HLS INTERFACE m_axi port = metrics offset = slave bundle = gmem_metrics
#pragma HLS INTERFACE m_axi port = status offset = slave bundle = gmem_status
    // 上面这些 m_axi bundle 会变成 kernel 的外部 memory master 端口。
    // connectivity_u55c.cfg 再把端口名绑定到具体 HBM pseudo-channel。
    // 例如 a_win_blocks 在 gmem_val 端口，当前配置里连到 HBM[1]。

    // 标量状态通常综合成寄存器：
    //   rz    = r^T z
    //   rr    = r^T r
    //   p_ap  = p^T A p
    //   alpha = rz / p_ap
    //   beta  = rz_new / rz_old
    data_t rz = 0.0;
    data_t rr = 0.0;
    data_t p_ap = 0.0;
    data_t alpha = 0.0;
    data_t beta = 0.0;
    int iterations = 0;
    int status_code = kStatusMaxIter;

    if (n <= 0 || max_iters < 0 || tau <= 0.0 || invalid_scalar(tau)) {
        // 这里处理的是配置错误，不是算法迭代中的 breakdown。
        // 直接 return 可以避免后续访问非法长度的 HBM BO。
        // 输入参数非法时，直接写 breakdown 状态。host 看到 status[0]=2
        // 后会把本次运行标记为失败。
        status[0] = kStatusBreakdown;
        status[1] = 0;
        metrics[0] = 0.0;
        metrics[1] = 0.0;
        metrics[2] = 0.0;
        metrics[3] = 0.0;
        return;
    }

    // 初始化阶段的第一步：ap BO 临时承载 ax0 = A*x0。
    // 如果 x0 是全 0，这次 SpMV 的结果也是全 0；但代码仍统一执行，
    // 这样 host 不需要根据 x0 特殊化流程。
    (void)spmv_blocked_windowed(a_win_row_ptr,
                                a_win_col_idx,
                                a_win_blocks,
                                x,
                                ap,
                                n,
                                false);

init_vectors:
    for (int index = 0; index < n; ++index) {
// HLS pragma: PIPELINE II=1 尝试把初始化向量循环流水化到每周期一个元素。
// 该循环同时读 b/ap/m_inv、写 r/z/p，并做 rz/rr 归约；归约链可能影响最终 II。
#pragma HLS PIPELINE II = 1
        // r0 = b - A*x0，z0 = M^{-1}r0，p0 = z0。
        // 同一趟循环里顺便归约 rz0 和 rr0。
        //
        // 这相当于把原来的 init_pcg_kernel 合并进当前控制 kernel。
        // rz/rr 是标量归约，后面控制 alpha/beta 和收敛出口。
        const data_t r_value = b[index] - ap[index];
        const data_t z_value = m_inv[index] * r_value;
        r[index] = r_value;
        z[index] = z_value;
        p[index] = z_value;
        rz += r_value * z_value;
        rr += r_value * r_value;
    }

pcg_loop:
    for (int iteration = 0; iteration < max_iters; ++iteration) {
        // 每一轮 PCG 入口处，当前状态是：
        //   x = x_k
        //   r = r_k
        //   z = z_k = M^{-1}r_k
        //   p = p_k
        //   rz = r_k^T z_k
        //   rr = r_k^T r_k
        //
        // 这里的判断和计算都在 FPGA 上完成，host 不参与逐轮控制。
        // 循环入口先检查 rr，覆盖“初始残差已经满足 tau”的情况。
        if (rr <= tau) {
            status_code = kStatusConverged;
            break;
        }
        // rz 太小或出现 NaN 时，alpha/beta 会失去意义，按 breakdown 退出。
        if (invalid_scalar(rz) || invalid_scalar(rr) || abs_value(rz) <= kBreakdownEps) {
            status_code = kStatusBreakdown;
            break;
        }

        // 主循环 SpMV：ap = A * p。这里复用同一个 block/bitmap 矩阵。
        // pAp = p^T ap 在 SpMV 的 row tile 写回阶段顺手归约出来，避免
        // 原来 SpMV 后独立 dot_loop 再从 HBM 读一遍 p/ap。
        // 这是 PCG 主循环中计算量最大的阶段，也是后续最值得继续优化
        // HBM 分片、window metadata 和多 worker 的地方。
        p_ap = spmv_blocked_windowed(a_win_row_ptr,
                                     a_win_col_idx,
                                     a_win_blocks,
                                     p,
                                     ap,
                                     n,
                                     true);

        if (invalid_scalar(p_ap) || abs_value(p_ap) <= kBreakdownEps) {
            // p^T A p 接近 0 表示方向失效或矩阵条件不满足 CG 假设。
            status_code = kStatusBreakdown;
            break;
        }

        // 保存旧 rz，后面 beta = rz_new / rz_old 要用。
        const data_t rz_old = rz;
        alpha = rz / p_ap;
        if (invalid_scalar(alpha)) {
            // p_ap 非法或 rz 非法会导致 alpha NaN；这里再兜底检查一次。
            status_code = kStatusBreakdown;
            break;
        }

        data_t rz_new = 0.0;
        data_t rr_new = 0.0;
update_xrz_loop:
        for (int index = 0; index < n; ++index) {
// HLS pragma: PIPELINE II=1 尝试把 x/r/z 更新和 rz/rr 归约融合循环流水化。
// 这里同时访问多个 HBM 端口，实际吞吐受端口、浮点乘加和归约依赖共同限制。
#pragma HLS PIPELINE II = 1
            // alpha 相关的向量更新全部融合在这一趟：
            //   x_{k+1}, r_{k+1}, z_{k+1}, rz_{k+1}, rr_{k+1}
            //
            // 这样比拆成多个循环少读写几次 HBM：
            //   p/ap/x/r/m_inv 读一次，x/r/z 写一次，同时完成两个归约。
            // 这部分相当于原来的 update_xrz_kernel。
            const data_t x_value = x[index] + alpha * p[index];
            const data_t r_value = r[index] - alpha * ap[index];
            const data_t z_value = m_inv[index] * r_value;
            x[index] = x_value;
            r[index] = r_value;
            z[index] = z_value;
            rz_new += r_value * z_value;
            rr_new += r_value * r_value;
        }

        rz = rz_new;
        rr = rr_new;
        iterations = iteration + 1;

        if (invalid_scalar(rz) || invalid_scalar(rr)) {
            // 到这里 x_{k+1}/r_{k+1}/z_{k+1} 已经写入 HBM。
            // 如果归约结果非法，继续算 beta/update_p 没有意义。
            status_code = kStatusBreakdown;
            break;
        }
        if (rr <= tau) {
            // 及时收敛出口：已经满足 tau，就不再为下一轮计算 beta/update_p。
            status_code = kStatusConverged;
            break;
        }
        if (abs_value(rz_old) <= kBreakdownEps) {
            // beta = rz_new / rz_old。rz_old 太小会放大误差或除零。
            status_code = kStatusBreakdown;
            break;
        }

        beta = rz / rz_old;
        if (invalid_scalar(beta)) {
            // 如果 beta 非法，下一轮 p 方向就不可用，按 breakdown 退出。
            status_code = kStatusBreakdown;
            break;
        }

update_p_loop:
        for (int index = 0; index < n; ++index) {
// HLS pragma: PIPELINE II=1 尝试让 p 向量更新每周期处理一个元素。
// 该循环读 z/p 并写 p，只有在未收敛且 beta 有效时才会执行。
#pragma HLS PIPELINE II = 1
            // p_{k+1} = z_{k+1} + beta * p_k。
            // 这部分相当于原来的 update_p_kernel。只有未收敛时才执行；
            // 如果上一段 rr <= tau，最终 x 已经可用，不需要再准备下一轮 p。
            p[index] = z[index] + beta * p[index];
        }
    }

    if (status_code == kStatusMaxIter && rr <= tau) {
        // 正常情况下，收敛会在循环入口或 update_xrz 后被捕获。
        // 这里是保险兜底：如果最后一轮正好达到 tau，也把状态修正成 converged。
        status_code = kStatusConverged;
    }

    // 只回写少量标量，供 host 打印摘要和判断最终状态。
    // x/r/z/p/ap 都已经在各自 HBM BO 中；host 目前只需要最终 x，所以
    // 不回读其他中间向量。metrics/status 是 host 判断运行结果的轻量摘要。
    metrics[0] = rz;
    metrics[1] = rr;
    metrics[2] = p_ap;
    metrics[3] = alpha;
    status[0] = status_code;
    status[1] = iterations;
}

}
