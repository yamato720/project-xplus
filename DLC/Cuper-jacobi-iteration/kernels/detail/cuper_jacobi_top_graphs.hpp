#pragma once

// CuperJacobiIteration 的顶层 task graph。
// 这里只声明 stream 并连接各 task，具体算法和 service 逻辑放在相邻 detail 头文件中。

#include <ap_int.h>
#include <tapa.h>

#include "jacobi_controller.hpp"
#include "jacobi_stage_timer.hpp"
#include "jacobi_update_service.hpp"
#include "jacobi_vector_loader.hpp"
#include "spmv_service_drains.hpp"
#include "spmv_service_tasks.hpp"

// TAPA Jacobi iteration 实验顶层。
//
// 当前 demo 保留 service 化 Cuper SpMV。host 侧先把 A 拆成 D+R，
// Jacobi_Vector_Loader 读 X0/X1 时取负，因此 Cuper service 在这里计算的是
// -R*x_old，后级完成 Jacobi update：
//
//   Jacobi_Controller
//       -> CuperSpmvServiceCommand
//       -> SpmvService_SpElementPtrLoader / Jacobi_Vector_Loader /
//          SpmvService_MatrixLoader
//       -> SpmvService_Core[0..15]
//       -> SpmvService_Accumulator[0..15]
//       -> SpmvService_VectorChecker[0..7]
//       -> SpmvService_MultSortTree
//       -> Jacobi_Update_Service
//       -> X0/X1
//
// update stage 直接消费 -R*x_old：
//   x_next = (b + (-R*x_old)) * diag_inv
void CuperJacobiIteration(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                          tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                          tapa::mmap<float_v16> B,
                          tapa::mmap<float_v16> Diag_inv,
                          tapa::mmap<float_v16> X0,
                          tapa::mmap<float_v16> X1,
                          tapa::mmap<INDEX_TYPE> Status,
                          tapa::mmap<double> Metrics,
                          const INDEX_TYPE Batch_num,
                          const INDEX_TYPE Matrix_len,
                          const INDEX_TYPE Row_num,
                          const INDEX_TYPE Column_num,
                          const INDEX_TYPE Max_iters,
                          const float Tau
                         ) {
    // Command_Stream[0] 给 SpElement ptr loader，Command_Stream[1] 给 X0/X1 vector loader。
    tapa::streams<CuperSpmvServiceCommand, 2, 4>               Command_Stream("Command_Stream");
    // 16 路矩阵 loader 各有一条 command stream，保证每轮同步读 Matrix_data[channel]。
    tapa::streams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM, 4> Matrix_Command_Stream("Matrix_Command_Stream");

    // PE_Param 和 Vector_X_Stream 是 16 级 Core 串接链；第 16 项是链尾 drain。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>        PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>         Vector_X_Stream("Vector_X_Stream");
    // Matrix_A_Stream 每路只进入对应 Core，不跨 HBM channel 转发。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 64>           Matrix_A_Stream("Matrix_A_Stream");

    // Core 输出局部乘积到对应 Accumulator；Accumulator 输出 float_v2 给 checker。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>             Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>         Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>              Vector_Y_Stream("Vector_Y_Stream");
    tapa::streams<float_v2, 8, FIFO_DEPTH>                     Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");

    // SpMV 最终 -Rx 包直接进入 Jacobi update，不再落到 standalone Cuper 的 Y_out。
    tapa::stream<float_v16, 128>                               Spmv_Stream("Spmv_Stream");
    tapa::stream<JacobiFrame, 2>                               Update_Frame_Stream("Update_Frame_Stream");
    tapa::stream<JacobiUpdateResult, 2>                        Update_Result_Stream("Update_Result_Stream");
    // Stage timer 独立统计 controller 等待期间的真实 cycle，用 Metrics[4..7] 回传。
    tapa::stream<JacobiStageEvent, 16>                         Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 8>                                Stage_Ticks_Stream("Stage_Ticks_Stream");

    // checker/sort/vector drain 在没有内嵌 stop token 的位置使用独立 stop stream。
    tapa::streams<INDEX_TYPE, 8, 2>                            Checker_Stop_Stream("Checker_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                                Sort_Stop_Stream("Sort_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                                Vector_Destroy_Stop_Stream("Vector_Destroy_Stop_Stream");

    tapa::task()
        // controller 是唯一的迭代级控制 task：发 command/frame、收 update result、写状态。
        .invoke(Jacobi_Controller,
                Command_Stream,
                Matrix_Command_Stream,
                Checker_Stop_Stream,
                Sort_Stop_Stream,
                Vector_Destroy_Stop_Stream,
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Update_Frame_Stream,
                Update_Result_Stream,
                Status,
                Metrics,
                Row_num,
                Max_iters,
                Tau)
        .invoke(Jacobi_Stage_Timer,
                Stage_Event_Stream,
                Stage_Ticks_Stream)
        // update service 消费 -Rx 和向量 HBM，计算 x_next 并回传 diff。
        .invoke(Jacobi_Update_Service,
                Update_Frame_Stream,
                Spmv_Stream,
                Update_Result_Stream,
                B,
                Diag_inv,
                X0,
                X1)
        // SpMV service 前端：边界表 loader、X0/X1 loader、16 路矩阵 loader。
        .invoke(SpmvService_SpElementPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Command_Stream[0],
                PE_Param[0])
        .invoke(Jacobi_Vector_Loader,
                Column_num,
                X0,
                X1,
                Command_Stream[1],
                Vector_X_Stream[0])
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoader,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_A_Stream)
        // 16 级 Core 串接：PE_Param 和 Vector_X_Stream 逐级转发，Matrix_A_Stream 每级独占。
        .invoke(SpmvService_Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        .invoke(SpmvService_Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        .invoke(SpmvService_Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        .invoke(SpmvService_Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        .invoke(SpmvService_Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        .invoke(SpmvService_Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        .invoke(SpmvService_Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        .invoke(SpmvService_Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        .invoke(SpmvService_Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        .invoke(SpmvService_Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        .invoke(SpmvService_Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        .invoke(SpmvService_Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        .invoke(SpmvService_Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        .invoke(SpmvService_Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        .invoke(SpmvService_Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        .invoke(SpmvService_Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 链尾 drain 只消费 Core 继续转发出来的尾流，不参与数学计算。
        .invoke(SpmvService_DestroyInt, PE_Param[HBM_CHANNEL_NUM])
        .invoke(SpmvService_DestroyFloatV16, Vector_X_Stream[HBM_CHANNEL_NUM], Vector_Destroy_Stop_Stream)
        // SpMV 后端：每路 accumulator 做局部行累加，checker 过滤 padding，sort 拼成 -Rx。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_Accumulator,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream)
        .invoke<tapa::join, 8>(SpmvService_VectorChecker,
                               Row_num,
                               Vector_Y_Stream,
                               Vector_Y_Stream_Aftck,
                               Checker_Stop_Stream)
        .invoke(SpmvService_MultSortTree,
                Vector_Y_Stream_Aftck,
                Spmv_Stream,
                Sort_Stop_Stream)
    ;
}
