#pragma once

// PCG scalar/vector compute stages for Callipepla.

#include "callipepla_common.hpp"

/*  PCG 计算模块：M1 为 SpMV，M2-M7 分别对应 alpha、x/r/z/rz/p 更新。 */

// M2: alpha = rzold / (p' * Ap)。rp=-1 是初始化轮，输出特殊 alpha 以对齐后续模块。
void dot_alpha(const int rp_time,
               const int M,
               //const unsigned long rz0,
               tapa::istream<double> & qrz,
               tapa::istream<double_v8> & q1,
               tapa::istream<double_v8> & q2,
               tapa::ostreams<double, 2> & q3,
               tapa::istream<bool> & q_gbc,
               tapa::ostream<bool> & q_gbc_out
               ) {
    //const InstCmp inst = {(M + 7) >> 3, 0.0, 0};
    const int num_ite = (M + 7) >> 3;
    double rzold = 0.0;

    bool term_flag = false;

rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        double psum[8][DEP_DIST_LOAD_STORE];
#pragma HLS array_partition complete variable=psum dim=1

    init:
        for (int i = 0; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            for (int p = 0; p < 8; ++p) {psum[p][i] = 0.0;}
        }

    comp1:
        for (int i = 0, idx = 0; i < num_ite; ) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            //DEBUG
            bool tmp1 = q1.empty();
            bool tmp2 = q2.empty();
            if (!q1.empty() & !q2.empty()) {
                double_v8 v1; q1.try_read(v1);
                double_v8 v2; q2.try_read(v2);
                for (int p = 0; p < 8; ++p) {
                    psum[p][idx] += ((i * 8 + p < M)? v1[p] * v2[p] : 0.0);
                }
                ++i;
                ++idx;
                if (idx == DEP_DIST_LOAD_STORE) {idx = 0;}
            }
        }

    comp2:
        for (int i = DEP_DIST_LOAD_STORE; i < DEP_DIST_LOAD_STORE * 8; ++i) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            psum[0][i % DEP_DIST_LOAD_STORE] += psum[i / DEP_DIST_LOAD_STORE][i % DEP_DIST_LOAD_STORE];
        }

    comp3:
        for (int i = 1; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            psum[0][0] += psum[0][i];
        }

        double pAp = psum[0][0];

        double alpha = rzold / pAp;
        double alpha_out[2];

        alpha_out[0] = alpha;
        alpha_out[1] = alpha;
        if (rp < 0) {
            alpha_out[0] = 0.0;
            alpha_out[1] = 1.0;
        }

        q3[0].write(alpha_out[0]);
        q3[1].write(alpha_out[1]);
        ap_wait();
        rzold = qrz.read();

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "@@@ exit dot_alpha\n";
}

// M end: res = r' * r，并根据阈值产生全局终止信号。
void dot_res(const int rp_time,
             const int M,
             const double th_termination,
             tapa::istream<double_v8> & q1,
             tapa::ostream<ResTerm> & q2,
             tapa::ostream<bool> & q_termination
             ) {
    //InstCmp inst = {(M + 7) >> 3, 0.0, 0};
    const int num_ite = (M + 7) >> 3;
    bool term_flag = false;

rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        double psum[8][DEP_DIST_LOAD_STORE];
#pragma HLS array_partition complete variable=psum dim=1

    init:
        for (int i = 0; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            for (int p = 0; p < 8; ++p) {psum[p][i] = 0.0;}
        }

    comp1:
        for (int i = 0, idx = 0; i < num_ite; ) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            if (!q1.empty()) {
                double_v8 v1; q1.try_read(v1);
                for (int p = 0; p < 8; ++p) {
                    psum[p][idx] += ((i * 8 + p < M)? v1[p] * v1[p] : 0.0);
                }
                ++i;
                ++idx;
                if (idx == DEP_DIST_LOAD_STORE) {idx = 0;}
            }
        }

    comp2:
        for (int i = DEP_DIST_LOAD_STORE; i < DEP_DIST_LOAD_STORE * 8; ++i) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            psum[0][i % DEP_DIST_LOAD_STORE] += psum[i / DEP_DIST_LOAD_STORE][i % DEP_DIST_LOAD_STORE];
        }

    comp3:
        for (int i = 1; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            psum[0][0] += psum[0][i];
        }

        ResTerm out_p;
        out_p.res = psum[0][0];

        //cout << "ite = " << rp << ", res = " << res << endl;
        term_flag = out_p.res < th_termination;
        out_p.term = term_flag;
        q2.write(out_p);

        ap_wait();
        q_termination.write(term_flag);
    }

    //cout << "$$$ exit dot_res\n";
}

// M6: rznew = r' * z，同时把 r 旁路给残差计算。
void dot_rznew(const int rp_time,
               const int M,
               tapa::istream<double_v8> & qr,
               tapa::istream<double_v8> & qz,
               tapa::ostream<double_v8> & qr_out,
               tapa::ostreams<double, 2> & qrz,
               tapa::istream<bool> & q_gbc,
               tapa::ostream<bool> & q_gbc_out
               ) {
    //InstCmp inst = {(M + 7) >> 3, 0.0, 0};
    const int num_ite = (M + 7) >> 3;
    bool term_flag = false;

rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        double psum[8][DEP_DIST_LOAD_STORE];
#pragma HLS array_partition complete variable=psum dim=1

    init:
        for (int i = 0; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            for (int p = 0; p < 8; ++p) {psum[p][i] = 0.0;}
        }

    comp1:
        for (int i = 0, idx = 0; i < num_ite; ) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            //DEBUG
            bool tmp1 = qr.empty();
            bool tmp2 = qz.empty();
            bool tmp3 = qr_out.full();
            if (!qr.empty() & !qz.empty() & !qr_out.full()) {
                double_v8 v1; qr.try_read(v1);
                double_v8 v2; qz.try_read(v2);
                qr_out.try_write(v1);
                for (int p = 0; p < 8; ++p) {
                    psum[p][idx] += ((i * 8 + p < M)? v1[p] * v2[p] : 0.0);
                }
                ++i;
                ++idx;
                if (idx == DEP_DIST_LOAD_STORE) {idx = 0;}
            }
        }

    comp2:
        for (int i = DEP_DIST_LOAD_STORE; i < DEP_DIST_LOAD_STORE * 8; ++i) {
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=psum distance=DEP_DIST_LOAD_STORE
            psum[0][i % DEP_DIST_LOAD_STORE] += psum[i / DEP_DIST_LOAD_STORE][i % DEP_DIST_LOAD_STORE];
        }

    comp3:
        for (int i = 1; i < DEP_DIST_LOAD_STORE; ++i) {
#pragma HLS pipeline II=1
            psum[0][0] += psum[0][i];
        }

        double rz = psum[0][0];

        qrz[0].write(rz);
        qrz[1].write(rz);

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "@@@ exit dot_rznew\n";
}

// M3: x = x + alpha * p。
void updt_x(const int rp_time,
            const int M,
            tapa::istream<double> & qalpha,
            tapa::istream<double_v8> & qx,
            tapa::istream<double_v8> & qp,
            tapa::ostream<double_v8> & qout,
            tapa::istream<bool> & q_gbc
            //tapa::ostream<bool> & q_gbc_out
            ) {
    //InstCmp inst = {(M + 7) >> 3, qalpha.read(), 0};
    const int num_ite = (M + 7) >> 3;
    bool term_flag = false;

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        const double alpha = qalpha.read();
        //qout = x + alpha .* p;
    cc:
        for (int i = 0; i < num_ite;) {
    #pragma HLS pipeline II=1
            if (!qx.empty() & !qp.empty()) {
                double_v8 tmpx; qx.try_read(tmpx);
                double_v8 tmpp; qp.try_read(tmpp);
                qout.write(tmpx + alpha * tmpp);
                ++i;
            }
        }

        ap_wait();
        term_flag = q_gbc.read();
        //q_gbc_out.write(term_flag);
    }
    //cout << "@@@ exit updt_x\n";
}

// M7: p = z + (rznew/rzold) * p，同时把旧 P 旁路给下一轮 x 更新。
void updt_p(const int rp_time,
            const int M,
            //const unsigned long rz0,
            tapa::istream<double> & qrznew,
            tapa::istream<double_v8> & qz,
            tapa::istream<double_v8> & qp,
            tapa::ostream<double_v8> & qp2m3,
            tapa::ostream<double_v8> & qout,
            tapa::istream<bool> & q_gbc,
            tapa::ostream<bool> & q_gbc_out
            ) {
    //InstCmp inst = {(M + 7) >> 3, rznew/rzold, 0};
    const int num_ite = (M + 7) >> 3;
    double rzold = 1.0;//tapa::bit_cast<double>(rz0);
    bool term_flag = false;

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        const double rznew = qrznew.read();
        double rzndo = rznew / rzold;
        if (rp < 0) {
            rzndo = 0.0;
        }

    cc:
        for (int i = 0; i < num_ite;) {
    #pragma HLS pipeline II=1
            if (!qz.empty() & !qp.empty() & !qp2m3.full()) {
                double_v8 tmpz; qz.try_read(tmpz);
                double_v8 tmpp; qp.try_read(tmpp);
                qp2m3.try_write(tmpp);
                qout.write(tmpz + rzndo * tmpp);
                ++i;
            }
        }

        rzold = rznew;

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "@@@@ exit updt_p\n";
}

// M4: r = r - alpha * Ap。这里按两拍阶段交错消费 alpha 和两条 residual 路径。
void updt_r(const int rp_time,
            const int M,
            tapa::istream<double> & qalpha,
            tapa::istream<double_v8> & qr,
            tapa::istream<double_v8> & qap,
            tapa::ostream<double_v8> & qout,
            tapa::istream<bool> & q_gbc,
            tapa::ostream<bool> & q_gbc_out
            ) {
    //InstCmp inst = {(M + 7) >> 3, alpha, 0};
    const int num_ite = (M + 7) >> 3;
    double alpha = 0.0;

    bool term_flag = false;

l_rp:
    for(int rp = -2; !term_flag & (rp < rp_time * 2); rp++) {
#pragma HLS loop_flatten off
        if ((rp &0x1) == 0) {
            alpha = qalpha.read();
        }

        //qout = x + alpha .* p;
    cc:
        for (int i = 0; i < num_ite;) {
#pragma HLS pipeline II=1
            if (!qr.empty() & !qap.empty()) {
                double_v8 tmpr;   qr.try_read(tmpr);
                double_v8 tmpap; qap.try_read(tmpap);
                qout.write(tmpr - alpha * tmpap);
                ++i;
            }
        }

        ap_wait();
        if ((rp &0x1) == 1) {
            term_flag = q_gbc.read();
            q_gbc_out.write(term_flag);
        }
    }

    //cout << "@@@@ exit updt_r\n";
}

// M5: z = diagA \ r，Jacobi 预条件等价于逐元素除以对角线。
void left_div(const int rp_time,
              const int M,
              tapa::istream<double_v8> & qr,
              tapa::istream<double_v8> & qdiagA,

              tapa::ostreams<double_v8, 2> & qz,

              tapa::ostream<double_v8> & qr_m6,
              tapa::ostream<double_v8> & qrmem,
              tapa::istream<bool> & q_gbc,
              tapa::ostream<bool> & q_gbc_out
              ) {
    //InstCmp inst1 = {(M + 7) >> 3, 0.0, 0};
    //InstCmp inst2 = {(M + 7) >> 3, 0.0, 1};
    InstCmp inst = {(M + 7) >> 3, 0.0, 0};
    bool term_flag = false;

rp:
    for(int rp = -2; !term_flag & (rp < rp_time * 2); rp++) {
#pragma HLS loop_flatten off
        inst.q_idx = rp&0x1;
    cc:
        for (int i = 0; i < inst.len; ) {
#pragma HLS pipeline II=1
            //DEBUG
            bool tmp1 = qr.empty();
            bool tmp2 = qdiagA.empty();
            bool nop_flag = qr.empty() | qdiagA.empty();
            if (inst.q_idx == 0) {
                nop_flag |= qr_m6.full();
            } else {
                nop_flag |= qrmem.full();
            }
            if (!nop_flag) {
                double_v8 v1; qr.try_read(v1);
                if (inst.q_idx == 0) {
                    qr_m6.try_write(v1);
                } else {
                    qrmem.try_write(v1);
                }
                double_v8 v2; qdiagA.try_read(v2);
                double_v8 result;
                for (int p = 0; p < 8; ++p) {
                    result[p] = ((i * 8 + p < M)? v1[p] / v2[p] : 0.0);
                }
                ++i;
                qz[inst.q_idx].write(result);
            }
        }

        ap_wait();
        if (inst.q_idx == 1) {
            term_flag = q_gbc.read();
            q_gbc_out.write(term_flag);
        }
    }

    //cout << "@@@ exit left_div\n";
}

// 将每轮 residual scalar 写回 vec_res；收到 term 后缩短写入次数。
void wr_r(const int rp_time,
          tapa::async_mmap<double> & vec_r,
          tapa::istream<ResTerm> & q_din
          ) {
    int wr_count = rp_time + 1;
wr:
    for (int addr_req = 0, i_resp = 0; i_resp < wr_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if ((addr_req < wr_count) &
            !q_din.empty() &
            !vec_r.write_addr.full() &
            !vec_r.write_data.full() ) {
            vec_r.write_addr.try_write(addr_req);
            ResTerm tmpv;
            q_din.try_read(tmpv);
            vec_r.write_data.try_write(tmpv.res);
            ++addr_req;
            if (tmpv.term) {wr_count = addr_req;}
        }
        uint8_t n_resp;
        if (vec_r.write_resp.try_read(n_resp)) {
            i_resp += int(n_resp) + 1;
        }

    }
    //cout << "$$$$ exit wr_r\n";
}

// 新 P 同时要写回 P buffer 并进入下一轮 SpMV，因此在这里复制成两路。
void duplicator(tapa::istream<double_v8> & q_in,
                tapa::ostream<double_v8> & q_out1,
                tapa::ostream<double_v8> & q_out2
                ) {
cc:
    for (;;) {
#pragma HLS pipeline II=1
        if (!q_in.empty() &
            !q_out1.full() &
            !q_out2.full()
            ) {
            double_v8 tmp;
            q_in.try_read(tmp);
            q_out1.try_write(tmp);
            q_out2.try_write(tmp);
        }
    }
}

// SpMV 的 P 输入选择器：第 0 次用内存初始 P，之后用 updt_p 产生的新 P。
void vecp_mux(const int rp_time,
              const int M,
              tapa::istream<bool> & q_gbc,

              tapa::istream<double_v8> & q_in1,
              tapa::istream<double_v8> & q_in2,

              tapa::ostream<double_v8> & q_out
              ) {
    const int num_ite = (M + 7) >> 3;
    bool term_flag = false;

    // deliver p form memory at ite 0
    q2q(q_in1,
        q_out,
        num_ite);

    for (int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
        term_flag = q_gbc.read();
        if (term_flag | (rp == rp_time - 1)) {
            clearq(q_in2, num_ite);
        } else {
            q2q(q_in2,
                q_out,
                num_ite);
        }
    }
}
