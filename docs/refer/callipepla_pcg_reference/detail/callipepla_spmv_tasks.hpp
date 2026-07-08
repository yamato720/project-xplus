#pragma once

// SpMV loader, PE, accumulator, merger, and stream drain tasks for Callipepla.

#include "callipepla_common.hpp"

// 读取每个窗口的边界指针，并把 NUM_ITE/M/rp_time 元信息送入 PE 链。
void read_edge_list_ptr(const int num_ite,
                        const int M,
                        const int rp_time, //P_N,
                        tapa::async_mmap<int> & edge_list_ptr,
                        tapa::ostream<int> & PE_inst,
                        tapa::istream<bool> & q_gbc,
                        tapa::ostream<bool> & q_gbc_out
                        ) {
    //const int rp_time = (P_N == 0)? 1 : P_N;

    PE_inst.write(num_ite);
    PE_inst.write(M);
    PE_inst.write(rp_time);
    //PE_inst.write(K);

    const int num_ite_plus1 = num_ite + 1;
    bool term_flag = false;

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    rd_ptr:
        for (int i_req = 0, i_resp = 0; i_resp < num_ite_plus1;) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            async_read(edge_list_ptr,
                       PE_inst,
                       num_ite_plus1,
                       i_req, i_resp);
        }

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "### exit read_edge_list_ptr" << endl;
}

// 每轮把完整 X/P 向量读入 SpMV PE 链，PE_Xvec 会按窗口缓存需要的片段。
void read_vec(const int rp_time, //P_N
              const int M, //K,
              tapa::async_mmap<double_v8> & vec_X,
              tapa::ostream<double_v8> & fifo_X
              ) {
    //const int rp_time = (P_N == 0)? 1 : P_N;
    const int num_ite_X = (M + 7) >> 3;

l_rp:
    for(int rp = 0; rp < rp_time; rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    rd_X:
        for(int i_req = 0, i_resp = 0; i_resp < num_ite_X;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            async_read(vec_X,
                       fifo_X,
                       num_ite_X,
                       i_req, i_resp);
        }
    }
}

// 从单一路 HBM 读取稀疏矩阵打包数据；16 个实例分别对应 16 个 matrix channel。
void read_A(const int rp_time, //P_N,
            const int A_len,
            tapa::async_mmap<ap_uint<512>> & A,
            tapa::ostream<ap_uint<512>> & fifo_A,
            tapa::istream<bool> & q_gbc,
            tapa::ostream<bool> & q_gbc_out
            ) {
    //const int rp_time = (P_N == 0)? 1 : P_N;
    bool term_flag = false;
l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    rd_A:
        for(int i_req = 0, i_resp = 0; i_resp < A_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
            async_read(A,
                       fifo_A,
                       A_len,
                       i_req, i_resp);
        }

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }

    //cout << "##### exit read_A\n";
}

// SpMV 前半段：缓存当前 X 窗口，解码 512-bit 矩阵包，输出 a_ij * x_j 和目标行号。
void PEG_Xvec(tapa::istream<int> & fifo_inst_in,
              tapa::istream<ap_uint<512>> & fifo_A,
              tapa::istream<double_v8> & fifo_X_in,
              tapa::ostream<int> & fifo_inst_out,
              tapa::ostream<double_v8> & fifo_X_out,
              // to PEG_Yvec
              tapa::ostream<int> & fifo_inst_out_to_Yvec,
              tapa::ostream<MultXVec> & fifo_aXvec,
              tapa::istream<bool> & q_gbc,
              tapa::ostream<bool> & q_gbc_out,
              tapa::ostream<bool> & q_gbc_out_Y
              ) {
    const int NUM_ITE = fifo_inst_in.read();
    const int M = fifo_inst_in.read();
    const int rp_time = fifo_inst_in.read();
    //const int K = fifo_inst_in.read();

    fifo_inst_out.write(NUM_ITE);
    fifo_inst_out.write(M);
    fifo_inst_out.write(rp_time);
    //fifo_inst_out.write(K);

    fifo_inst_out_to_Yvec.write(NUM_ITE);
    fifo_inst_out_to_Yvec.write(M);
    fifo_inst_out_to_Yvec.write(rp_time);

    bool term_flag = false;

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16

        double local_X[4][WINDOW_SIZE];
#pragma HLS bind_storage variable=local_X latency=1
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

        auto start_32 = fifo_inst_in.read();
        fifo_inst_out.write(start_32);
        fifo_inst_out_to_Yvec.write(start_32);

    main:
        for (int i = 0; i < NUM_ITE; ++i) {
#pragma HLS loop_tripcount min=1 max=49

            // 按窗口填充片上 X cache，同时把 X 包继续传给后级 PE，形成 PE 链。
        read_X:
            for (int j = 0; (j < WINDOW_SIZE_div_8) & (j < ((M + 7) >> 3) - i * WINDOW_SIZE_div_8); ) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II = 1
                if (!fifo_X_in.empty() & !fifo_X_out.full()) {
                    double_v8 x; fifo_X_in.try_read(x);
                    fifo_X_out.try_write(x);
                    for (int kk = 0; kk < 8; ++kk) { //512 / 64 = 8
                        for (int l = 0; l < 4; ++l) {
                            local_X[l][(j << 3) + kk] = x[kk]; // 8 -> << 3
                        }
                    }
                    ++j;
                }
            }

            // 解码矩阵包并完成乘法；row[17] 置位表示 padding 项，不参与累加。
            const auto end_32 = fifo_inst_in.read();
            fifo_inst_out.write(end_32);
            fifo_inst_out_to_Yvec.write(end_32);

        computation:
            for (int j = start_32; j < end_32; ) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
                if (!fifo_A.empty()) {
                    ap_uint<512> a_pes; fifo_A.try_read(a_pes);
                    MultXVec raxv;

                    for (int p = 0; p < 8; ++p) {
                        ap_uint<64> a = a_pes(63 + p * 64, p * 64);
                        ap_uint<14> a_col = a(63, 50);
                        ap_uint<18> a_row = a(49, 32);
                        ap_uint<32> a_val = a(31,  0);

                        raxv.row[p] = a_row;
                        if (a_row[17] == 0) {
                            float a_val_f32 = tapa::bit_cast<float>(a_val);
                            double a_val_f64 = (double) a_val_f32;
                            raxv.axv[p] = a_val_f64 * local_X[p/2][a_col];
                        }
                    }
                    fifo_aXvec.write(raxv);
                    ++j;
                }
            }
            start_32 = end_32;
        }

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
        q_gbc_out_Y.write(term_flag);
    }
    //cout << "##### exit PEG_Xvec\n";
}

// SpMV 后半段：按行号在 URAM 中累加每个 PE 的局部 Y 分量。
void PEG_Yvec(tapa::istream<int> & fifo_inst_in,
              tapa::istream<MultXVec> & fifo_aXvec,
              tapa::ostream<double> & fifo_Y_out,
              tapa::istream<bool> & q_gbc
              ) {
    const int NUM_ITE = fifo_inst_in.read();
    const int M = fifo_inst_in.read();
    const int rp_time = fifo_inst_in.read();

    const int num_v_init = (M + NUM_CH_SPARSE_mult_8 - 1) / NUM_CH_SPARSE_mult_8;
    const int num_v_out = (M + NUM_CH_SPARSE - 1) / NUM_CH_SPARSE;

    bool term_flag = false;

    double local_C[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_C type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_C dim=1

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16

        // 每轮 SpMV 前清空局部累加缓冲。
    init_C:
        for (int i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            for (int p = 0; p < 8; ++p) {
                local_C[p][i] = 0.0;
            }
        }

        auto start_32 = fifo_inst_in.read();

    main:
        for (int i = 0; i < NUM_ITE; ++i) {
#pragma HLS loop_tripcount min=1 max=49

            // computation
            const auto end_32 = fifo_inst_in.read();

        computation:
            for (int j = start_32; j < end_32; ) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=local_C distance=DEP_DIST_LOAD_STORE
                if (!fifo_aXvec.empty()) {
                    MultXVec raxv; fifo_aXvec.try_read(raxv);
                    for (int p = 0; p < 8; ++p) {
                        auto a_row = raxv.row[p];
                        if (a_row[17] == 0) {
                            local_C[p][a_row] += raxv.axv[p];
                        }
                    }
                    ++j;
                }
            }
            start_32 = end_32;
        }

        // 以 PE/lane 交错顺序输出，后续 Arbiter/Merger 再恢复 double_v8 包。
    write_C_outer:
        for (int i = 0, c_idx = 0; i < num_v_out; ++i) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            double out_v = local_C[c_idx][i>>3];
            fifo_Y_out.write(out_v);
            ++c_idx;
            if (c_idx == 8) {c_idx = 0;}
        }

        ap_wait();
        term_flag = q_gbc.read();
    }
    //cout << "#### exit PEG_Yvec\n";
}

// 轮询 NUM_CH_SPARSE/8 路 PE 输出，剔除 padding 后送入 8 路 merger。
void Arbiter_Y(const int rp_time, //P_N,
               const int M,
               tapa::istreams<double, NUM_CH_SPARSE_div_8> & fifo_in, // 2 = 16 / 8
               tapa::ostream<double> & fifo_out,
               tapa::istream<bool> & q_gbc,
               tapa::ostream<bool> & q_gbc_out
               ) {
    //const int rp_time = (P_N == 0)? 1 : P_N;
    const int num_pe_output = ((M + NUM_CH_SPARSE - 1) / NUM_CH_SPARSE) * NUM_CH_SPARSE_div_8;
    const int num_out = (M + 7) >> 3;
    const int num_ite_Y = num_pe_output * (rp_time + 1);

    bool term_flag = false;

l_rp:
    for(int rp = -1; !term_flag & (rp < rp_time); rp++) {
    aby:
        for (int i = 0, c_idx = 0; i < num_pe_output;) {
    #pragma HLS loop_tripcount min=1 max=1800
    #pragma HLS pipeline II=1
            if (!fifo_in[c_idx].empty() & !fifo_out.full()) {
                double tmp; fifo_in[c_idx].try_read(tmp);
                if (i < num_out) {
                    fifo_out.try_write(tmp);
                }
                ++i;
                c_idx++;
                if (c_idx == NUM_CH_SPARSE_div_8) {c_idx = 0;}
            }
        }

        ap_wait();
        term_flag = q_gbc.read();
        q_gbc_out.write(term_flag);
    }
    //cout << "### exit Arbiter_Y\n";
}

// 收齐 8 个 scalar Y，打包为 double_v8，同时分发给 AP 内存和后续 dot/updt 路径。
void Merger_Y(tapa::istreams<double, 8> & fifo_in,
              tapa::ostreams<double_v8, 2> & fifo_out) {
    for (;;) {
#pragma HLS pipeline II=1
        bool flag_nop = fifo_out[0].full() | fifo_out[1].full();
        for (int i = 0; i < 8; ++i) {
            flag_nop |= fifo_in[i].empty();
        }
        if (!flag_nop) {
            double_v8 tmpv;
#pragma HLS aggregate variable=tmpv
            for (int i = 0; i < 8; ++i) {
                double tmp; fifo_in[i].try_read(tmp);
                tmpv[i] = tmp;
            }
            fifo_out[0].try_write(tmpv);
            fifo_out[1].try_write(tmpv);
        }
    }
}

// black-hole task 用来消费 task graph 链尾无下游的转发 token，避免上游满 FIFO 卡住。
template <typename data_t>
inline void bh(tapa::istream<data_t> & q) {
#pragma HLS inline
    for (;;) {
#pragma HLS pipeline II=1
        data_t tmp; q.try_read(tmp);
    }
}

void black_hole_int(tapa::istream<int> & fifo_in) {
    bh(fifo_in);
}

void black_hole_double_v8(tapa::istream<double_v8> & fifo_in) {
    bh(fifo_in);
}

void black_hole_bool(tapa::istream<bool> & fifo_in) {
    bh(fifo_in);
}
