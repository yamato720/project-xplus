#include "detail/callipepla_spmv_tasks.hpp"
#include "detail/callipepla_vector_tasks.hpp"
#include "detail/callipepla_pcg_tasks.hpp"

void Callipepla(tapa::mmap<int> edge_list_ptr,

                tapa::mmaps<ap_uint<512>, NUM_CH_SPARSE> edge_list_ch,

                tapa::mmaps<double_v8, 2> vec_x,

                tapa::mmaps<double_v8, 2> vec_p,

                tapa::mmap<double_v8> vec_Ap,

                tapa::mmaps<double_v8, 2> vec_r,

                tapa::mmap<double_v8> vec_digA,

                tapa::mmap<double> vec_res,

                const int NUM_ITE,
                const int NUM_A_LEN,
                const int M,
                const int rp_time,
                const double th_termination
                ) {

    // SpMV 通路 FIFO：edge pointer -> PEG_Xvec -> PEG_Yvec -> Arbiter -> Merger。

    tapa::streams<int, NUM_CH_SPARSE + 1, FIFO_DEPTH> PE_inst("PE_inst");

    tapa::streams<double_v8, NUM_CH_SPARSE + 1, FIFO_DEPTH> fifo_P_pe("fifo_P_pe");

    tapa::streams<ap_uint<512>, NUM_CH_SPARSE, FIFO_DEPTH> fifo_A("fifo_A");

    tapa::streams<int, NUM_CH_SPARSE, FIFO_DEPTH> Yvec_inst("Yvec_inst");

    tapa::streams<MultXVec, NUM_CH_SPARSE, FIFO_DEPTH> fifo_aXvec("fifo_aXvec");

    tapa::streams<double, NUM_CH_SPARSE, FIFO_DEPTH> fifo_Y_pe("fifo_Y_pe");

    tapa::streams<double, 8, FIFO_DEPTH> fifo_Y_pe_abd("fifo_Y_pe_abd");

    // 向量模块 FIFO：各向量均拆成 memory 指令、内存读出、写入数据和写响应。

    // P: search direction，双缓冲并同时服务 SpMV、dot_alpha、updt_x/updt_p。
    tapa::streams<InstRdWr, 2, FIFO_DEPTH> fifo_mi_P("fifo_mi_P");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_din_P("fifo_din_P");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_dout_P("fifo_dout_P");

    tapa::streams<bool, 2, FIFO_DEPTH> fifo_resp_P("fifo_resp_P");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_dot("fifo_P_dot");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_updtx("fifo_P_updtx");

    tapa::stream<double_v8, 2> fifo_P_updtp("fifo_P_updtp");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_updated("fifo_P_updated");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_to_dup("fifo_P_to_dup");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_to_mux("fifo_P_to_mux");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_P_from_mem("fifo_P_from_mem");

    // R: residual，双缓冲并分发给 residual norm、left_div、dot_rznew 和写回。
    tapa::streams<InstRdWr, 2, FIFO_DEPTH> fifo_mi_R("fifo_mi_R");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_din_R("fifo_din_R");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_dout_R("fifo_dout_R");

    tapa::streams<bool, 2, FIFO_DEPTH> fifo_resp_R("fifo_resp_R");

    tapa::stream<double_v8, 2> fifo_R("fifo_R");
    //to m4

    tapa::stream<double_v8, FIFO_DEPTH> fifo_R_updtd_m5("fifo_R_updtd_m5");
    //to m5

    tapa::stream<double_v8, FIFO_DEPTH_M6> fifo_R_updtd_m6("fifo_R_updtd_m6");
    //to m6

    tapa::stream<double_v8, FIFO_DEPTH> fifo_R_updtd_rr("fifo_R_updtd_rr");
    //to rr

    tapa::stream<double_v8, FIFO_DEPTH> fifo_R_tomem("fifo_R_tomem");

    tapa::stream<ResTerm, FIFO_DEPTH> fifo_RR("fifo_RR");

    // diagA: Jacobi 预条件的对角线向量，只读。
    tapa::stream<double_v8, FIFO_DEPTH> fifo_dA("fifo_dA");

    // X: 解向量，双缓冲读旧值并写入 updt_x 结果。
    tapa::streams<InstRdWr, 2, FIFO_DEPTH> fifo_mi_X("fifo_mi_X");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_din_X("fifo_din_X");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_dout_X("fifo_dout_X");

    tapa::streams<bool, 2, FIFO_DEPTH> fifo_resp_X("fifo_resp_X");

    tapa::stream<double_v8, 2> fifo_X("fifo_X");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_X_updated("fifo_X_updated");

    // AP: SpMV 结果 A*p，写入后被 alpha 点积和 residual 更新复用。
    tapa::stream<InstRdWr, FIFO_DEPTH> fifo_mi_AP("fifo_mi_AP");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_din_AP("fifo_din_AP");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_dout_AP("fifo_dout_AP");

    tapa::stream<bool, FIFO_DEPTH> fifo_resp_AP("fifo_resp_AP");

    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_AP_M1("fifo_AP_M1");

    tapa::stream<double_v8, FIFO_DEPTH> fifo_AP("fifo_AP");

    // Z: 预条件 residual，left_div 输出给 rznew 点积和 p 更新。
    tapa::streams<double_v8, 2, FIFO_DEPTH> fifo_Z("fifo_Z");

    // 标量 FIFO：alpha 和 rz 在多个阶段间广播。
    tapa::streams<double, 2, FIFO_DEPTH> fifo_alpha("fifo_alpha");

    tapa::streams<double, 2, FIFO_DEPTH> fifo_rz("fifo_rz");

    // 终止信号链：dot_res 产生后逐级通知仍在跑 rp 循环的 task 退出。
    tapa::stream<bool, FIFO_DEPTH> tsignal_res("tsignal_res");

    tapa::streams<bool, NUM_CH_SPARSE + 1, FIFO_DEPTH> tsignal_rdA("tsignal_rdA");

    tapa::streams<bool, NUM_CH_SPARSE + 1 + 1, FIFO_DEPTH> tsignal_edgepointer("tsignal_edgepointer");

    tapa::streams<bool, NUM_CH_SPARSE, FIFO_DEPTH> tsignal_Y("tsignal_Y");

    tapa::streams<bool, 8 + 1, FIFO_DEPTH> tsignal_aby("tsignal_aby");

    tapa::stream<bool, FIFO_DEPTH> tsignal_toM3("tsignal_toM3");

    tapa::stream<bool, FIFO_DEPTH> tsignal_toM4("tsignal_toM4");

    tapa::stream<bool, FIFO_DEPTH> tsignal_toM5("tsignal_toM5");

    tapa::stream<bool, FIFO_DEPTH> tsignal_toM6("tsignal_toM6");

    tapa::stream<bool, FIFO_DEPTH> tsignal_toM7("tsignal_toM7");

    tapa::stream<bool, FIFO_DEPTH> tsignal_ctrlP("tsignal_ctrlP");

    tapa::stream<bool, FIFO_DEPTH> tsignal_ctrlAP("tsignal_ctrlAP");

    tapa::stream<bool, FIFO_DEPTH> tsignal_ctrldigA("tsignal_ctrldigA");

    tapa::stream<bool, FIFO_DEPTH> tsignal_ctrlR("tsignal_ctrlR");

    tapa::stream<bool, FIFO_DEPTH> tsignal_ctrlX("tsignal_ctrlX");

    tapa::stream<bool, FIFO_DEPTH> tsignal_mux("tsignal_mux");

    /* =========deploy modules======= */

    // task graph 从终止信号广播开始，随后并行部署 SpMV、向量内存和 PCG 计算模块。
    tapa::task()
        .invoke<tapa::detach>(term_signal_router,
                              tsignal_res,
                              tsignal_rdA,
                              tsignal_edgepointer,
                              tsignal_aby,
                              tsignal_ctrlP,
                              tsignal_mux
                              )

        .invoke<tapa::join>(read_edge_list_ptr,
                NUM_ITE,
                M,
                rp_time,
                edge_list_ptr,
                PE_inst,
                tsignal_edgepointer,
                tsignal_edgepointer
                )

    // P 内存和控制。
        .invoke<tapa::detach, 2>(rdwr_vec,
                              vec_p,
                              fifo_mi_P,
                              fifo_dout_P,
                              fifo_din_P,
                              fifo_resp_P
                              )

        .invoke<tapa::join>(ctrl_P,
                            fifo_din_P,
                            fifo_dout_P,
                            rp_time,
                            M,
                            fifo_mi_P,
                            fifo_P_from_mem,
                            fifo_P_dot,
                            //fifo_P_updtx,
                            fifo_P_updtp,
                            fifo_P_updated,
                            fifo_resp_P,
                            tsignal_ctrlP,
                            tsignal_ctrlAP
                            )
        // P mux 负责初始 P 与 updt_p 新 P 之间切换，输出给 SpMV PE 链。
        .invoke<tapa::join>(vecp_mux,
                            rp_time,
                            M,
                            tsignal_mux,
                            fifo_P_from_mem,
                            fifo_P_to_mux,
                            fifo_P_pe
                            )

        .invoke<tapa::join, NUM_CH_SPARSE>(read_A,
                                           rp_time,
                                           NUM_A_LEN,
                                           edge_list_ch,
                                           fifo_A,
                                           tsignal_rdA,
                                           tsignal_rdA
                                           )
        .invoke<tapa::detach>(black_hole_bool,
                              tsignal_rdA)

        .invoke<tapa::join, NUM_CH_SPARSE>(PEG_Xvec,
                                           PE_inst,
                                           fifo_A,
                                           fifo_P_pe,
                                           PE_inst,
                                           fifo_P_pe,
                                           Yvec_inst,
                                           fifo_aXvec,
                                           tsignal_edgepointer,
                                           tsignal_edgepointer,
                                           tsignal_Y
                                           )
        .invoke<tapa::detach>(black_hole_int,
                              PE_inst)
        .invoke<tapa::detach>(black_hole_double_v8,
                              fifo_P_pe)
        .invoke<tapa::detach>(black_hole_bool,
                              tsignal_edgepointer)

        .invoke<tapa::join, NUM_CH_SPARSE>(PEG_Yvec,
                                           Yvec_inst,
                                           fifo_aXvec,
                                           fifo_Y_pe,
                                           tsignal_Y
                                           )

        .invoke<tapa::join, 8>(Arbiter_Y,
                               rp_time,
                               M,
                               fifo_Y_pe,
                               fifo_Y_pe_abd,
                               tsignal_aby,
                               tsignal_aby
                               )

        .invoke<tapa::detach>(Merger_Y,
                              fifo_Y_pe_abd,
                              fifo_AP_M1
                              )

    // 其余向量内存模块。

    // R 内存和控制。
        .invoke<tapa::detach, 2>(rdwr_vec,
                              vec_r,
                              fifo_mi_R,
                              fifo_dout_R,
                              fifo_din_R,
                              fifo_resp_R
                              )

        .invoke<tapa::join>(ctrl_R,
                            fifo_din_R,
                            fifo_dout_R,
                            rp_time,
                            M,
                            fifo_mi_R,
                            fifo_R,
                            fifo_R_tomem,
                            fifo_resp_R,
                            tsignal_ctrlR,
                            tsignal_ctrlX
                            )

    // diagA 只读控制。
        .invoke<tapa::join>(read_digA,
                            vec_digA,
                            rp_time,
                            M,
                            fifo_dA,
                            tsignal_ctrldigA,
                            tsignal_ctrlR
                            )

    // X 内存和控制。
        .invoke<tapa::detach, 2>(rdwr_vec,
                              vec_x,
                              fifo_mi_X,
                              fifo_dout_X,
                              fifo_din_X,
                              fifo_resp_X
                              )

        .invoke<tapa::join>(ctrl_X,
                            fifo_din_X,
                            fifo_dout_X,
                            rp_time,
                            M,
                            fifo_mi_X,
                            fifo_X,
                            fifo_X_updated,
                            fifo_resp_X,
                            tsignal_ctrlX
                            )

    // AP 内存和控制。
        .invoke<tapa::detach>(rdwr_vec,
                              vec_Ap,
                              fifo_mi_AP,
                              fifo_dout_AP,
                              fifo_din_AP,
                              fifo_resp_AP
                              )

        .invoke<tapa::join>(ctrl_AP,
                            fifo_din_AP,
                            fifo_dout_AP,
                            rp_time,
                            M,
                            fifo_mi_AP,
                            fifo_AP,
                            fifo_AP_M1,
                            fifo_resp_AP,
                            tsignal_ctrlAP,
                            tsignal_ctrldigA
                            )

    // PCG 向量/标量计算模块。

    //M2: alpha = rzold / (p' * Ap)
        .invoke<tapa::join>(dot_alpha,
                            rp_time,
                            M,
                            //rz0,
                            fifo_rz,
                            fifo_P_dot,
                            fifo_AP_M1,
                            fifo_alpha,
                            tsignal_aby,
                            tsignal_toM4
                            )

    //M3: x = x + alpha * p
        .invoke<tapa::join>(updt_x,
                            rp_time,
                            M,
                            fifo_alpha,
                            fifo_X,
                            fifo_P_updtx,
                            fifo_X_updated,
                            tsignal_toM3
                            )

    //M4: r = r - alpha * Ap
        .invoke<tapa::join>(updt_r,
                            rp_time,
                            M,
                            fifo_alpha,
                            fifo_R,
                            fifo_AP,
                            fifo_R_updtd_m5,
                            tsignal_toM4,
                            tsignal_toM5
                            )

    //M5: z = diagA \ r
        .invoke<tapa::join>(left_div,
                            rp_time,
                            M,
                            fifo_R_updtd_m5,
                            fifo_dA,
                            fifo_Z,
                            fifo_R_updtd_m6,
                            fifo_R_tomem,
                            tsignal_toM5,
                            tsignal_toM6
                            )

    //M6: rznew = r' * z
        .invoke<tapa::join>(dot_rznew,
                            rp_time,
                            M,
                            fifo_R_updtd_m6,
                            fifo_Z,
                            fifo_R_updtd_rr,
                            fifo_rz,
                            tsignal_toM6,
                            tsignal_toM7
                            )


    //M7: p = z + (rznew/rzold) * p
        .invoke<tapa::join>(updt_p,
                            rp_time,
                            M,
                            //rz0,
                            fifo_rz,
                            fifo_Z,
                            fifo_P_updtp,
                            fifo_P_updtx,
                            fifo_P_to_dup,
                            tsignal_toM7,
                            tsignal_toM3
                            )
        .invoke<tapa::detach>(duplicator,
                              fifo_P_to_dup,
                              fifo_P_updated,
                              fifo_P_to_mux
                              )

    //M residual
        .invoke<tapa::join>(dot_res,
                            rp_time,
                            M,
                            th_termination,
                            fifo_R_updtd_rr,
                            fifo_RR,
                            tsignal_res
                            )

        .invoke<tapa::join>(wr_r,
                            rp_time,
                            vec_res,
                            fifo_RR
                            )
    ;
}
