#pragma once

// Vector memory controllers for P/AP/X/R/diagA in the Callipepla PCG pipeline.

#include "callipepla_common.hpp"

// P 双缓冲控制：初始 P 送 SpMV，随后为 dot(alpha) 和 p 更新分别读出旧 P。
void ctrl_P(tapa::istreams<double_v8, 2> & qm_din,
            tapa::ostreams<double_v8, 2> & qm_dout,
            const int rp_time, //P_N
            const int M, //K,
            tapa::ostreams<InstRdWr, 2> & q_inst,
            tapa::ostream<double_v8> & q_spmv,
            tapa::ostream<double_v8> & q_dotp,
            //tapa::ostream<double_v8> & q_updtx,
            tapa::ostream<double_v8> & q_updtp,
            tapa::istream<double_v8> & q_updated,
            tapa::istreams<bool, 2> & q_res,
            tapa::istream<bool> & q_gbc,
            tapa::ostream<bool> & q_gbc_out
            ) {
    // InstVCtrl vinst1 = {true, false, 0, (M + 7) >> 3, q_spmv};
    // InstVCtrl vinst2 = {true, false, 0, (M + 7) >> 3, q_dotp};
    // InstVCtrl vinst3 = {true, true, 0, (M + 7) >> 3, q_dotp};
    const int num_ite_M = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -1, ch = 0; !term_flag & (rp < rp_time); rp++, ch = 1 - ch) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        InstRdWr ist;

        // 初始迭代从内存读取 P 送给 SpMV，后续由 vecp_mux 选择更新后的 P。
        ist.rd = true;
        ist.wr = false;
        //ist.require_response = false;//0;
        ist.base_addr = 0;
        ist.len = num_ite_M;

        if (rp == -1) {
            q_inst[ch].write(ist);
            ap_wait();
            q2q(qm_din,
                q_spmv,
                ist.len,
                ch);
            ap_wait();
        }

        // 读取 P 与 Ap 做 p'Ap，用于计算 alpha。
        //ist.rdwr = false;//0;
        //ist.base_addr = 0;
        //ist.len = num_ite;
        q_inst[ch].write(ist);
        ap_wait();
        q2q(qm_din,
            q_dotp,
            ist.len,
            ch);
        ap_wait();

        // 读旧 P 给 updt_p，同时把上轮新 P 写到另一片 buffer。

        //ist.rdwr = false;//0;
        //ist.base_addr = 0;
        //ist.len = num_ite;
        q_inst[ch].write(ist);
        ap_wait();

        ist.rd = false;
        ist.wr = true;
        //ist.base_addr = 0;
        //ist.len = num_ite;
        q_inst[1 - ch].write(ist);
        ap_wait();

        qq2qq(q_updated,
              qm_dout,
              qm_din,
              q_updtp,
              ist.len,
              1 - ch);

        ap_wait();
        q_res[1 - ch].read();

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "## exit ctrl_P\n";
}

// AP 控制：先写入当前 SpMV 结果，再读出两遍分别供 r 更新和 alpha 点积使用。
void ctrl_AP(tapa::istream<double_v8> & qm_din,
             tapa::ostream<double_v8> & qm_dout,

             const int rp_time,
             const int M,
             tapa::ostream<InstRdWr> & q_inst,

             tapa::ostream<double_v8> & q_updr,
             tapa::istream<double_v8> & q_pe,
             tapa::istream<bool> & q_res,
             tapa::istream<bool> & q_gbc,
             tapa::ostream<bool> & q_gbc_out
             ) {
    // InstVCtrl vinst1 = {false, true, 0, (M + 7) >> 3, q_dout};
    // InstVCtrl vinst2 x 2 = {true, false, 0, (M + 7) >> 3, q_updr};

    const int num_ite_M = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -1, ch = 0; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        InstRdWr ist;

        // 写入本轮 A*p 结果。
        ist.rd = false;
        ist.wr = true;
        ist.base_addr = 0;
        ist.len = num_ite_M;

        q_inst.write(ist);
        ap_wait();
        q2q(q_pe,
            qm_dout,
            ist.len);
        ap_wait();
        q_res.read();
        ap_wait();

        // 读出 Ap 给 updt_r 和 dot_alpha 两个消费者。
        ist.rd = true;
        ist.wr = false;
        //ist.base_addr = 0;
        //ist.len = num_ite;

        for (int l = 0; l < 2; ++l) {
#pragma HLS loop_flatten off
            q_inst.write(ist);
            ap_wait();
            q2q(qm_din,
                q_updr,
                ist.len);
        }

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "## exit ctrl_AP\n";
}

// X 双缓冲控制：读旧 X 给 updt_x，同时把新 X 写回另一片 buffer。
void ctrl_X(tapa::istreams<double_v8, 2> & qm_din,
            tapa::ostreams<double_v8, 2> & qm_dout,

            const int rp_time,
            const int M,
            tapa::ostreams<InstRdWr, 2> & q_inst,

            tapa::ostream<double_v8> & q_oldx,
            tapa::istream<double_v8> & q_newx,
            tapa::istreams<bool, 2> & q_res,
            tapa::istream<bool> & q_gbc
            //tapa::ostream<bool> & q_gbc_out
            ) {
    // InstVCtrl vinst = {true, true, 0, (M + 7) >> 3, 0};
    const int num_ite_M = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -1, ch = 0; !term_flag & (rp < rp_time); rp++, ch = 1 - ch) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        InstRdWr ist;
        ist.rd = true;
        ist.wr = false;
        ist.base_addr = 0;
        ist.len = num_ite_M;

        q_inst[ch].write(ist);
        ap_wait();

        ist.rd = false;
        ist.wr = true;
        q_inst[1 - ch].write(ist);
        ap_wait();

        qq2qq(q_newx,
              qm_dout,
              qm_din,
              q_oldx,
              ist.len,
              1 - ch);

        ap_wait();
        q_res[1 - ch].read();

        ap_wait();
        term_flag = q_gbc.read();
        //q_gbc_out.write(term_flag);
    }

    //cout << "## exit ctrl_X\n";
}

// R 双缓冲控制：为 left_div、dot_res/dot_rznew 和写回路径分发当前 residual。
void ctrl_R(tapa::istreams<double_v8, 2> & qm_din,
            tapa::ostreams<double_v8, 2> & qm_dout,

            const int rp_time,
            const int M,
            tapa::ostreams<InstRdWr, 2> & q_inst,

            tapa::ostream<double_v8> & qr_to_pe,
            tapa::istream<double_v8> & qr_from_pe,

            tapa::istreams<bool, 2> & q_res,
            tapa::istream<bool> & q_gbc,
            tapa::ostream<bool> & q_gbc_out
            ) {
    // InstVCtrl vinst1 = {true, false, 0, (M + 7) >> 3, 0};
    // InstVCtrl vinst2 = {true, true, 0, (M + 7) >> 3, 0};
    const int num_ite_M = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -1, ch = 0; !term_flag & (rp < rp_time); rp++, ch = 1 - ch) {
#pragma HLS loop_flatten off
        InstRdWr ist;

        //0 -- rd: q_din -> qr_to_pe
        ist.rd = true;
        ist.wr = false;
        ist.base_addr = 0;
        ist.len = num_ite_M;

        q_inst[ch].write(ist);
        ap_wait();
        q2q(qm_din,
            qr_to_pe,
            ist.len,
            ch);
        ap_wait();

        q_inst[ch].write(ist);
        ap_wait();

        ist.rd = false;
        ist.wr = true;
        q_inst[1 - ch].write(ist);
        ap_wait();

        qq2qq(qr_from_pe,
              qm_dout,
              qm_din,
              qr_to_pe,
              ist.len,
              1 - ch);

        ap_wait();
        q_res[1 - ch].read();

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "## exit ctrl_R\n";
}

// 对角预条件向量 diagA 每轮读两次，分别服务 M5 的两条交错 z/r 数据路径。
void read_digA(tapa::async_mmap<double_v8> & vec_mem,
               const int rp_time, //P_N
               const int M, //K,
               tapa::ostream<double_v8> & q_dout,
               tapa::istream<bool> & q_gbc,
               tapa::ostream<bool> & q_gbc_out
               ) {
    // InstVCtrl vinst = {true, false, 0, (M + 7) >> 3, 0};
    const int num_ite_M = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -2; !term_flag & (rp < rp_time * 2); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    rd:
        for (int addr_req = 0, i_resp = 0; i_resp < num_ite_M;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            async_read(vec_mem,
                       q_dout,
                       num_ite_M,
                       addr_req, i_resp);
        }

        ap_wait();
        if (rp & 0x1) {
            term_flag = q_gbc.read();
            q_gbc_out.write(term_flag);
        }
    }

    //cout << "## exit ctrl_diagA\n";
}
