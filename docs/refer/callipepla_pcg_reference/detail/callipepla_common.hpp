#pragma once

// Private implementation details for Callipepla. Included only by ../callipepla.cpp.

#include <ap_int.h>
#include <cstdio>
#include <cstring>
#include <cassert>

#include "ap_utils.h"
#include <tapa.h>
#include "../callipepla.h"

// 常规 stream 深度保持较浅，M6 的 r/z 点积输入更长，单独加深以吸收阶段间抖动。
constexpr int FIFO_DEPTH = 2;
constexpr int FIFO_DEPTH_M6 = 50;

// 派生常量用于把 16 路稀疏 PE、8-wide 向量包和窗口大小换成循环边界。
const int NUM_CH_SPARSE_div_8 = NUM_CH_SPARSE / 8;
const int NUM_CH_SPARSE_mult_8 = NUM_CH_SPARSE * 8;
const int WINDOW_SIZE_div_8 = WINDOW_SIZE / 8;

// 一个矩阵包解码后的 8 个非零元乘 X 结果，row 最高位作为 padding/无效标志。
struct MultXVec {
    tapa::vec_t<ap_uint<18>, 8> row;
    double_v8 axv;
};

// 向量内存读写指令：rd/wr 可同时开启，用于一边读旧值一边写新值。
struct InstRdWr {
    bool rd;
    bool wr;
    //bool require_response;
    int base_addr;
    int len;
};

// 保留的向量控制指令格式；Callipepla 当前主路径直接下发 InstRdWr。
struct InstVCtrl {
    bool rd;
    bool wr;
    int base_addr;
    int len;
    ap_uint<3> q_rd_idx;
    //ap_uint<3> q_wr_idx;
};

// 标量计算阶段的统一指令，len 为 double_v8 包数量，q_idx 选择输出路。
struct InstCmp {
    int len;
    double alpha;
    ap_uint<3> q_idx;
};

// 残差与终止标志一起回写，wr_r 可据此提前停止写残差序列。
struct ResTerm {
    double res;
    bool term;
};

// 非阻塞 HBM 读 helper：每拍尽量发一个地址，同时把返回数据推入 FIFO。
template <typename T, typename R>
inline void async_read(tapa::async_mmap<T> & A,
                       tapa::ostream<T> & fifo_A,
                       const R i_end_addr,
                       R & i_req,
                       R & i_resp) {
#pragma HLS inline
    if ((i_req < i_end_addr) &
        !A.read_addr.full()) {
        A.read_addr.try_write(i_req);
        ++i_req;
    }
    if (!fifo_A.full() & !A.read_data.empty()) {
        T tmp;
        A.read_data.try_read(tmp);
        fifo_A.try_write(tmp);
        ++i_resp;
    }
}


// 非阻塞 HBM 写 helper：地址、数据和写响应解耦，避免阻塞流水。
template <typename T, typename R>
inline void async_write(tapa::async_mmap<T> & Y_out,
                        tapa::istream<T> & fifo_Y,
                        const R num_ite_Y,
                        R & i_req,
                        R & i_resp
                        ) {
#pragma HLS inline
    if ((i_req < num_ite_Y) &
        !fifo_Y.empty() &
        !Y_out.write_addr.full() &
        !Y_out.write_data.full() ) {
        Y_out.write_addr.try_write(i_req);
        T tmpv;
        fifo_Y.try_read(tmpv);
        Y_out.write_data.try_write(tmpv);
        ++i_req;
    }
    uint8_t n_resp;
    if (Y_out.write_resp.try_read(n_resp)) {
        i_resp += R(n_resp) + 1;
    }
}

// 通用向量 HBM 访问 task。控制器通过 InstRdWr 描述本轮读/写区间。
void rdwr_vec(tapa::async_mmap<double_v8> & vec_p,
              tapa::istream<InstRdWr> & q_inst,
              tapa::istream<double_v8> & q_din,
              tapa::ostream<double_v8> & q_dout,
              tapa::ostream<bool> & q_response
              ) {
    for (;;) {
        auto inst = q_inst.read();

        const int rd_end_addr = inst.rd? (inst.base_addr + inst.len) : 0;
        const int wr_end_addr = inst.wr? (inst.base_addr + inst.len) : 0;

        const int rd_total = inst.rd? inst.len : 0;
        const int wr_total = inst.wr? inst.len : 0;

    rdwr:
        // 读请求、读响应、写请求、写响应四个计数独立推进，可实现读写重叠。
        for (int rd_req = inst.base_addr, rd_resp = 0,
             wr_req = inst.base_addr, wr_resp = 0;
             (rd_resp < rd_total) | (wr_resp < wr_total);) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            // rd
            if ((rd_req < rd_end_addr) &
                !vec_p.read_addr.full()) {
                vec_p.read_addr.try_write(rd_req);
                ++rd_req;
            }
            if (!q_dout.full() & !vec_p.read_data.empty()) {
                double_v8 tmp;
                vec_p.read_data.try_read(tmp);
                q_dout.try_write(tmp);
                ++rd_resp;
            }

            //wr
            if ((wr_req < wr_end_addr) &
                !q_din.empty() &
                !vec_p.write_addr.full() &
                !vec_p.write_data.full() ) {
                vec_p.write_addr.try_write(wr_req);
                double_v8 tmpv;
                q_din.try_read(tmpv);
                vec_p.write_data.try_write(tmpv);
                ++wr_req;
            }
            uint8_t n_resp;
            if (vec_p.write_resp.try_read(n_resp)) {
                wr_resp += int(n_resp) + 1;
            }
        }

        ap_wait();

        if (inst.wr){
            q_response.write(true);
        }
    }
}

// 按固定 token 数把一个 stream 搬到另一个 stream，常用于连接 memory task 和计算 task。
template <typename data_t>
inline void q2q(tapa::istream<data_t> & qin,
                tapa::ostream<data_t> & qout,
                const int num_ite) {
#pragma HLS inline
q:
    for(int i = 0; i < num_ite;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if (!qin.empty() & !qout.full()) {
            data_t tmp;
            qin.try_read(tmp);
            qout.try_write(tmp);
            ++i;
        }
    }
}

// 丢弃固定数量 token，用于终止后清空不再消费的旁路数据。
template <typename data_t>
inline void clearq(tapa::istream<data_t> & qin,
                   const int num_ite) {
#pragma HLS inline
q:
    for(int i = 0; i < num_ite;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if (!qin.empty()) {
            data_t tmp;
            qin.try_read(tmp);
            ++i;
        }
    }
}

// 从双路输入中选择一路搬运，idx 由双缓冲控制器给出。
template <typename data_t>
inline void q2q(tapa::istreams<data_t, 2> & qin,
                tapa::ostream<data_t> & qout,
                const int num_ite,
                const int idx) {
#pragma HLS inline
q:
    for(int i = 0; i < num_ite;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if (!qin[idx].empty() & !qout.full()) {
            data_t tmp;
            qin[idx].try_read(tmp);
            qout.try_write(tmp);
            ++i;
        }
    }
}

// 双缓冲搬运：把计算结果写入下一路 buffer，同时从另一路读出旧值给下游。
template <typename data_t>
inline void qq2qq(tapa::istream<data_t> & qin_pe,
                  tapa::ostreams<data_t, 2> & qout_mem,
                  tapa::istreams<data_t, 2> & qin_mem,
                  tapa::ostream<data_t> & qout_qe,
                  const int num_ite,
                  const int idx) {
#pragma HLS inline
qq:
    for(int i = 0; i < num_ite;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if (!qin_pe.empty() & !qout_mem[idx].full()) {
            data_t tmp;
            qin_pe.try_read(tmp);
            qout_mem[idx].try_write(tmp);
            ++i;
        }
        if (!qin_mem[1 - idx].empty() & !qout_qe.full()) {
            data_t tmp;
            qin_mem[1 - idx].try_read(tmp);
            qout_qe.try_write(tmp);
        }
    }
}


// 把残差阶段产生的全局终止标志广播给仍在循环的读矩阵、PE、控制器和 mux。
void term_signal_router(tapa::istream<bool> & q_gbc,
                        tapa::ostream<bool> & q_to_rdA,
                        tapa::ostream<bool> & q_to_edgepointer,
                        tapa::ostream<bool> & q_to_abiter,
                        tapa::ostream<bool> & q_to_ctrlmem,
                        tapa::ostream<bool> & q_to_mux
                        ) {
spin:
    for (;;) {
#pragma HLS pipeline II=1
        if (!q_gbc.empty() &
            !q_to_rdA.full() &
            !q_to_edgepointer.full() &
            !q_to_abiter.full() &
            !q_to_ctrlmem.full() &
            !q_to_mux.full()
            ) {
            bool tmp;
            q_gbc.try_read(tmp);
            q_to_rdA.try_write(tmp);
            q_to_edgepointer.try_write(tmp);
            q_to_abiter.try_write(tmp);
            q_to_ctrlmem.try_write(tmp);
            q_to_mux.try_write(tmp);
        }
    }
}
