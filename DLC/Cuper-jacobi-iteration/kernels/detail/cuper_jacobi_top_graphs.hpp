#pragma once

// CuperJacobiIteration 的顶层 task graph。
// 这里只声明 stream 并连接各 task，具体算法和 service 逻辑放在相邻 detail 头文件中。

#include <ap_int.h>
#include <tapa.h>

#include "jacobi_controller.hpp"
#ifdef JACOBI_TRACE_ENABLED
#include "jacobi_deadlock_debug.hpp"
#endif
#include "jacobi_stage_timer.hpp"
#include "jacobi_cuper_output_update.hpp"
#include "jacobi_vector_loader.hpp"
#include "spmv_service_drains.hpp"
#include "spmv_service_tasks.hpp"

// TAPA Jacobi iteration 实验顶层。
//
// 当前 demo 保留 service 化 Cuper SpMV。host 侧先把 A 拆成 D+R，
// Jacobi_Vector_Loader 读 X 时取负，因此 Cuper service 的输出侧直接完成：
//
//   Jacobi_RoundTokenSource/Mux/Dispatcher
//       -> CuperSpmvServiceCommand + JacobiFrame
//       -> SpmvService_SpElementPtrLoader / Jacobi_Vector_Loader /
//          SpmvService_MatrixLoader
//       -> SpmvService_Core[0..15]
//       -> SpmvService_Accumulator[0..15]
//       -> Jacobi_UpdatePairCompute[0..7]
//          Jacobi_UpdateCoeffLoader ----^
//       -> Jacobi_UpdatePackWriter
//       -> X_Write_Stream
//       -> Jacobi_XHbmWriter
//       -> X
//
// Cuper 输出更新 stage 直接消费 accumulator 输出，在 update 内过滤 padding 并写回：
//   x_next = (b + (-R*x_old)) * diag_inv
void CuperJacobiIteration(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                          tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                          tapa::mmap<float_v16> B,
                          tapa::mmap<float_v16> Diag_inv,
                          tapa::mmap<float_v16> X,
                          tapa::mmap<INDEX_TYPE> Status,
                          tapa::mmap<double> Metrics,
#ifdef JACOBI_TRACE_ENABLED
                          tapa::mmap<INDEX_TYPE> Debug,
#endif
                          const INDEX_TYPE Batch_num,
                          const INDEX_TYPE Matrix_len,
                          const INDEX_TYPE Row_num,
                          const INDEX_TYPE Column_num,
                          const INDEX_TYPE Max_iters,
                          const float Tau
                         ) {
    // 显式控制流一览。表里只列算法自己定义的 token/frame/command/stop/event；
    // TAPA stream 的 full/empty 反压和 AXI mmap 的 ready/valid 属于底层流控，不在这里展开。
    //
    // 连接表：
    // 名字                                      作用                                      源                                      目标
    // ----------------------------------------  ----------------------------------------  --------------------------------------  -----------------------------------------
    // Initial_Token_Stream                      种第 0 轮或空迭代 stop token              Jacobi_RoundTokenSource                 Jacobi_RoundTokenMux
    // Feedback_Token_Stream                     反馈下一轮或最终 stop token               Jacobi_XHbmWriter                        Jacobi_RoundTokenMux
    // Round_Token_Stream                        统一轮次驱动 token                       Jacobi_RoundTokenMux                     Jacobi_RoundDispatcher
    // Command_Stream[0]                         启动/停止 SpElement ptr loader            Jacobi_RoundDispatcher                   SpmvService_SpElementPtrLoader
    // Command_Stream[1]                         启动/停止 X loader                        Jacobi_RoundDispatcher                   Jacobi_Vector_Loader
    // Matrix_Prefetch_Command_Stream[0..15]     启动/停止 16 路矩阵预取                   Jacobi_RoundDispatcher                   SpmvService_MatrixLoader[0..15]
    // PE_Param[0..16]                           Core 串接参数流，内嵌 stop token          SpmvService_SpElementPtrLoader/Core      SpmvService_Core[0..15]/DestroyInt
    // Vector_Y_Param[0..15]                     Accumulator 参数流，内嵌 stop token       SpmvService_Core[0..15]                  SpmvService_Accumulator[0..15]
    // Update_Frame_Stream                       update 后端轮次 frame                     Jacobi_RoundDispatcher                   Jacobi_UpdateFrameFork
    // Update_Coeff_Frame_Stream                 B/Diag_inv 读取 frame                     Jacobi_UpdateFrameFork                   Jacobi_UpdateCoeffLoader
    // Update_Pair_Frame_Stream[0..7]            8 路 pair compute 轮次/停止 frame          Jacobi_UpdateFrameFork                   Jacobi_UpdatePairCompute[0..7]
    // Update_Pack_Frame_Stream                  更新拼包 frame                            Jacobi_UpdateFrameFork                   Jacobi_UpdatePackWriter
    // Update_Hbm_Frame_Stream                   X HBM 写回 frame                          Jacobi_UpdateFrameFork                   Jacobi_XHbmWriter
    // X_Write_Stream                            x_next 写回 FIFO                          Jacobi_UpdatePackWriter                  Jacobi_XHbmWriter
    // Vector_Destroy_Stop_Stream                Vector_X 链尾 drain 独立停止令牌          Jacobi_RoundDispatcher                   SpmvService_DestroyFloatV16
    // Stage_Event_Stream                        计时事件，不参与数学控制                  Jacobi_RoundDispatcher                   Jacobi_Stage_Timer
    // Stage_Ticks_Stream                        计时结果回传                              Jacobi_Stage_Timer                       Jacobi_RoundDispatcher
    //
    // 时序表：
    // 名字                                      发出时对应完成了啥事                                      接收时要做什么
    // ----------------------------------------  --------------------------------------------------------  ------------------------------------------------------------
    // Initial_Token_Stream                      kernel 启动后已拿到 Row_num/Max_iters                    作为第一枚轮次 token 转入统一轮次流
    // Feedback_Token_Stream                     当前轮 X 写回响应已收齐，SpMV+update 计时已结束           作为下一轮 token 继续 dispatch，或转发 stop
    // Round_Token_Stream                        初始 token 或上一轮反馈 token 已就绪                     扇出 SpMV command、矩阵预取 command 和 update frame；stop 时收尾
    // Command_Stream[0]                         本轮 token 已被 dispatch；stop 时所有正常轮次已完成       读取边界表并写 PE_Param[0]，或向 PE_Param 写 stop token
    // Command_Stream[1]                         本轮 token 已被 dispatch；单 X 已由上一轮 writer 写完     非空 R 时读 X 并取负；空 R 时不读 X；stop 时退出
    // Matrix_Prefetch_Command_Stream[0..15]     第 0 轮 dispatch 前，或当前轮 compute command 发出后       从 Matrix_data 读 A/R 到 Matrix_A_Stream FIFO，或退出
    // PE_Param[0..16]                           边界表参数已读出，或收到 compute stop                    配置本级 Core 的行块范围并继续转发；stop 时逐级退出
    // Vector_Y_Param[0..15]                     Core 已确认本轮参数，或收到 PE stop                       Accumulator 按行数归并本通道乘积；stop 时退出
    // Update_Frame_Stream                       本轮 compute command 已发出                              复制给系数 loader 和 writer；stop frame 让 update 后端退出
    // Update_Coeff_Frame_Stream                 update frame 已被拆分                                    按 packet_count 读取 B 和 Diag_inv，分发到 8 路 pair compute
    // Update_Pair_Frame_Stream[0..7]            update frame 已被拆分                                    每路 pair compute 按 frame 消费固定数量；stop frame 后显式退出
    // Update_Pack_Frame_Stream                  update frame 已被拆分                                    收齐 8 路更新结果，拼成 float_v16 后写入 X_Write_Stream
    // Update_Hbm_Frame_Stream                   update frame 已被拆分                                    从 X_Write_Stream 连续写 HBM；响应收齐后反馈下一轮或 stop
    // X_Write_Stream                            一个 x_next float_v16 包已经拼好                         按地址顺序写入单 X buffer；FIFO 只解耦反压，不改变轮次边界
    // Vector_Destroy_Stop_Stream                dispatcher 收到最终 stop token，X loader stop 已发出      链尾 drain 等已知 X 包数 drain 完后退出
    // Stage_Event_Stream                        每轮 dispatch/feedback 或最终 stop                       统计 cycle；stop 后输出计数
    // Stage_Ticks_Stream                        timer 收到 stop 并完成 cycle 汇总                        dispatcher 读取后写 Metrics[4..7]
    // Command_Stream[0] 给 SpElement ptr loader，Command_Stream[1] 给 X vector loader。
    tapa::streams<CuperSpmvServiceCommand, 2, 4>               Command_Stream("Command_Stream");
    // 16 路矩阵 loader 各有一条预取 command stream；它只负责把 Matrix_data 提前灌入 FIFO。
    tapa::streams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM, 4> Matrix_Prefetch_Command_Stream("Matrix_Prefetch_Command_Stream");

    // PE_Param 和 Vector_X_Stream 是 16 级 Core 串接链；第 16 项是链尾 drain。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>        PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>         Vector_X_Stream("Vector_X_Stream");
    // Matrix_A_Stream 是矩阵预取 FIFO。Core 只有收到 PE 参数后才消费这里的数据；
    // FIFO 满时 matrix loader 自然反压，避免无限预取。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 1024>         Matrix_A_Stream("Matrix_A_Stream");

    // Core 输出局部乘积到对应 Accumulator；Accumulator 输出 float_v2 直接进入 Jacobi update。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>             Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>         Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>              Vector_Y_Stream("Vector_Y_Stream");

    tapa::stream<JacobiFrame, 2>                               Update_Frame_Stream("Update_Frame_Stream");
    tapa::stream<JacobiFrame, 2>                               Update_Coeff_Frame_Stream("Update_Coeff_Frame_Stream");
    tapa::streams<JacobiFrame, 8, 2>                            Update_Pair_Frame_Stream("Update_Pair_Frame_Stream");
    tapa::stream<JacobiFrame, 2>                               Update_Pack_Frame_Stream("Update_Pack_Frame_Stream");
    tapa::stream<JacobiFrame, 2>                               Update_Hbm_Frame_Stream("Update_Hbm_Frame_Stream");
    tapa::stream<JacobiRoundToken, 2>                          Initial_Token_Stream("Initial_Token_Stream");
    tapa::stream<JacobiRoundToken, 2>                          Feedback_Token_Stream("Feedback_Token_Stream");
    tapa::stream<JacobiRoundToken, 2>                          Round_Token_Stream("Round_Token_Stream");
    // Stage timer 独立统计真实 cycle，用 Metrics[4..7] 回传；由 dispatcher 单路发事件。
    tapa::stream<JacobiStageEvent, 16>                         Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 8>                                Stage_Ticks_Stream("Stage_Ticks_Stream");

    // vector drain 没有内嵌 stop token，使用独立 stop stream。
    tapa::stream<INDEX_TYPE, 2>                                Vector_Destroy_Stop_Stream("Vector_Destroy_Stop_Stream");
    tapa::streams<JacobiCoeffPair, 8, FIFO_DEPTH>              Update_Coeff_Stream("Update_Coeff_Stream");
    tapa::streams<JacobiUpdatedPair, 8, FIFO_DEPTH>            Update_Pair_Stream("Update_Pair_Stream");
    // 写回 FIFO 解耦 update 拼包和 AXI HBM 写响应抖动；下一轮 token 仍由 HBM writer
    // 在 write response 全部收齐后发出，保证单个 X buffer 不会读到半新半旧数据。
    tapa::stream<float_v16, 128>                               X_Write_Stream("X_Write_Stream");
#ifdef JACOBI_TRACE_ENABLED
    // trace/debug 只在 JACOBI_TRACE_LIGHT、JACOBI_TRACE_ISOTOPE 或
    // JACOBI_DEADLOCK_DEBUG 打开时存在。light 模式接关键控制、matrix loader0
    // 首拍、pair update 和写回节点；full 模式额外接全部 matrix/accumulator
    // 细节节点。业务 task 只 try_write
    // 非阻塞事件，DebugMonitor 汇总写 Debug HBM，避免 debug 通路制造新反压。
    tapa::stream<JacobiDebugEvent, 16>                            Debug_Dispatcher_Stream("Debug_Dispatcher_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_PtrLoader_Stream("Debug_PtrLoader_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_VectorLoader_Stream("Debug_VectorLoader_Stream");
    tapa::streams<JacobiDebugEvent, HBM_CHANNEL_NUM, 16>          Debug_MatrixLoader_Stream("Debug_MatrixLoader_Stream");
#ifdef JACOBI_TRACE_FULL
    tapa::streams<JacobiDebugEvent, HBM_CHANNEL_NUM, 16>          Debug_Accumulator_Stream("Debug_Accumulator_Stream");
#endif
    tapa::stream<JacobiDebugEvent, 16>                            Debug_FrameFork_Stream("Debug_FrameFork_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_CoeffLoader_Stream("Debug_CoeffLoader_Stream");
    tapa::streams<JacobiDebugEvent, kJacobiDebugPairStreamCount, 16> Debug_Pair_Stream("Debug_Pair_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_PackWriter_Stream("Debug_PackWriter_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_HbmWriter_Stream("Debug_HbmWriter_Stream");
    tapa::stream<INDEX_TYPE, 2>                                   Debug_Stop_Stream("Debug_Stop_Stream");
#endif

    tapa::task()
        // 数据流 token 只在开始时种一次；后续 token 由 Cuper 输出更新 stage 反馈生成。
        .invoke(Jacobi_RoundTokenSource,
                Initial_Token_Stream,
                Row_num,
                Max_iters)
        // 合并初始 token 和写回端反馈 token，形成统一的轮次驱动流。
        .invoke(Jacobi_RoundTokenMux,
                Initial_Token_Stream,
                Feedback_Token_Stream,
                Round_Token_Stream)
        // Dispatcher 不主动控制轮次，只按 token 扇出 command/frame；stop token 到达后统一收尾。
        .invoke(Jacobi_RoundDispatcher,
                Round_Token_Stream,
                Command_Stream,
                Matrix_Prefetch_Command_Stream,
                Vector_Destroy_Stop_Stream,
#ifdef JACOBI_TRACE_ENABLED
                Debug_Dispatcher_Stream,
                Debug_Stop_Stream,
#endif
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Update_Frame_Stream,
                Status,
                Metrics,
                Row_num,
                Max_iters,
                Tau)
        // 根据 dispatcher 发出的事件统计分段 cycle，并把结果回传给 dispatcher 写 Metrics。
        .invoke(Jacobi_Stage_Timer,
                Stage_Event_Stream,
                Stage_Ticks_Stream)
#ifdef JACOBI_TRACE_ENABLED
        // 非阻塞 debug monitor。它定期写 heartbeat，并记录最后一次收到的事件。
        .invoke(Jacobi_DebugMonitor,
                Debug_Dispatcher_Stream,
                Debug_PtrLoader_Stream,
                Debug_VectorLoader_Stream,
                Debug_MatrixLoader_Stream,
#ifdef JACOBI_TRACE_FULL
                Debug_Accumulator_Stream,
#endif
                Debug_FrameFork_Stream,
                Debug_CoeffLoader_Stream,
                Debug_Pair_Stream,
                Debug_PackWriter_Stream,
                Debug_HbmWriter_Stream,
                Debug_Stop_Stream,
                Debug)
#endif
        // 读取 SpElement 边界表，把每轮每个 PE 的行块参数送入 Core 串接链首端。
        .invoke(SpmvService_SpElementPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Command_Stream[0],
                PE_Param[0]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_PtrLoader_Stream
#endif
                )
        // 从 HBM 读取当前 X，取负后写入向量广播链首端，使 Core 输出 -R*x_old。
        // Batch_num=0 时 Core 不会消费 X，loader 会跳过读取，让 update 看到 -R*x=0。
        .invoke(Jacobi_Vector_Loader,
                Batch_num,
                Column_num,
                X,
                Command_Stream[1],
                Vector_X_Stream[0]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_VectorLoader_Stream
#endif
                )
        // 16 路矩阵 loader 按预取 command 从各自 HBM channel 拉取矩阵数据到 FIFO。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoader,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Prefetch_Command_Stream,
                                             Matrix_A_Stream
                                             ,
                                             tapa::seq()
#ifdef JACOBI_TRACE_ENABLED
                                             ,
                                             Debug_MatrixLoader_Stream
#endif
                                             )
        // 16 级 Core 串接：PE_Param 和 Vector_X_Stream 逐级转发，Matrix_A_Stream 每级独占。
        // Core 0 消费 HBM0 的矩阵分片，与广播到本级的 -X 相乘并输出局部乘积。
        .invoke(SpmvService_Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        // Core 1 消费 HBM1 的矩阵分片，并把 PE 参数和 -X 继续转发给下一级。
        .invoke(SpmvService_Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        // Core 2 消费 HBM2 的矩阵分片，并输出本通道对应的 row/value 乘积流。
        .invoke(SpmvService_Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        // Core 3 消费 HBM3 的矩阵分片，完成本通道的稀疏元素乘向量。
        .invoke(SpmvService_Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        // Core 4 消费 HBM4 的矩阵分片，并保持 PE/X 串接链继续向后传递。
        .invoke(SpmvService_Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        // Core 5 消费 HBM5 的矩阵分片，产生第 5 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        // Core 6 消费 HBM6 的矩阵分片，产生第 6 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        // Core 7 消费 HBM7 的矩阵分片，产生第 7 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        // Core 8 消费 HBM8 的矩阵分片，产生第 8 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        // Core 9 消费 HBM9 的矩阵分片，产生第 9 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        // Core 10 消费 HBM10 的矩阵分片，产生第 10 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        // Core 11 消费 HBM11 的矩阵分片，产生第 11 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        // Core 12 消费 HBM12 的矩阵分片，产生第 12 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        // Core 13 消费 HBM13 的矩阵分片，产生第 13 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        // Core 14 消费 HBM14 的矩阵分片，产生第 14 路局部 SpMV 乘积。
        .invoke(SpmvService_Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        // Core 15 消费 HBM15 的矩阵分片，产生最后一路局部乘积并转发链尾残余流。
        .invoke(SpmvService_Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 消费 PE 参数链尾残余，保证上游 Core 的参数转发不会悬空阻塞。
        .invoke(SpmvService_DestroyInt, PE_Param[HBM_CHANNEL_NUM])
        // 消费 -X 广播链尾残余。这里按 Column_num/Max_iters 算出应到达链尾的总包数，
        // drain 完后再接受 stop，避免链尾 drain 早退造成 Core15 卡住。
        .invoke(SpmvService_DestroyFloatV16,
                Batch_num,
                Column_num,
                Max_iters,
                Vector_X_Stream[HBM_CHANNEL_NUM],
                Vector_Destroy_Stop_Stream)
        // 每路 accumulator 对 Core 输出的局部乘积按行累加，得到本通道 SpMV 结果。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_Accumulator,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream
                                             ,
                                             tapa::seq()
#ifdef JACOBI_TRACE_FULL
                                             ,
                                             Debug_Accumulator_Stream
#endif
                                             )
        // dispatcher 的 frame 同时驱动系数 loader、8 路 pair compute、pack writer 和 HBM writer。
        .invoke(Jacobi_UpdateFrameFork,
                Update_Frame_Stream,
                Update_Coeff_Frame_Stream,
                Update_Pack_Frame_Stream,
                Update_Hbm_Frame_Stream,
                Update_Pair_Frame_Stream
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_FrameFork_Stream
#endif
                )
        // 按更新 frame 顺序读取 B 和 Diag_inv，为每个 row pair 提供 Jacobi 更新系数。
        .invoke(Jacobi_UpdateCoeffLoader,
                Update_Coeff_Frame_Stream,
                Update_Coeff_Stream,
                B,
                Diag_inv
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_CoeffLoader_Stream
#endif
                )
        // 8 个 pair compute 各自消费两路 accumulator 输出；有效位置做 Jacobi 更新，
        // padding 位置只读掉 -Rx，不再单独经过 checker 对齐。
        // 这里改为 frame/stop 驱动的有限 task，避免 detach 无限循环让 Finish 收尾不可观察。
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[0],
                Vector_Y_Stream[0],
                Vector_Y_Stream[1],
                Update_Coeff_Stream[0],
                Update_Pair_Stream[0]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[0],
                kJacobiDebugSourcePairBase + 0
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[1],
                Vector_Y_Stream[2],
                Vector_Y_Stream[3],
                Update_Coeff_Stream[1],
                Update_Pair_Stream[1]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[1],
                kJacobiDebugSourcePairBase + 1
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[2],
                Vector_Y_Stream[4],
                Vector_Y_Stream[5],
                Update_Coeff_Stream[2],
                Update_Pair_Stream[2]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[2],
                kJacobiDebugSourcePairBase + 2
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[3],
                Vector_Y_Stream[6],
                Vector_Y_Stream[7],
                Update_Coeff_Stream[3],
                Update_Pair_Stream[3]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[3],
                kJacobiDebugSourcePairBase + 3
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[4],
                Vector_Y_Stream[8],
                Vector_Y_Stream[9],
                Update_Coeff_Stream[4],
                Update_Pair_Stream[4]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[4],
                kJacobiDebugSourcePairBase + 4
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[5],
                Vector_Y_Stream[10],
                Vector_Y_Stream[11],
                Update_Coeff_Stream[5],
                Update_Pair_Stream[5]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[5],
                kJacobiDebugSourcePairBase + 5
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[6],
                Vector_Y_Stream[12],
                Vector_Y_Stream[13],
                Update_Coeff_Stream[6],
                Update_Pair_Stream[6]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[6],
                kJacobiDebugSourcePairBase + 6
#endif
                )
        .invoke(Jacobi_UpdatePairCompute,
                Update_Pair_Frame_Stream[7],
                Vector_Y_Stream[14],
                Vector_Y_Stream[15],
                Update_Coeff_Stream[7],
                Update_Pair_Stream[7]
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_Pair_Stream[7],
                kJacobiDebugSourcePairBase + 7
#endif
                )
        // pack writer 只收齐 8 路 float_v2 并拼成 float_v16，先写入 X_Write_Stream。
        .invoke(Jacobi_UpdatePackWriter,
                Update_Pack_Frame_Stream,
                Update_Pair_Stream,
                X_Write_Stream
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_PackWriter_Stream
#endif
                )
        // HBM writer 从 X_Write_Stream 连续写 X；write response 收齐后再反馈下一轮 token。
        .invoke(Jacobi_XHbmWriter,
                Update_Hbm_Frame_Stream,
                X_Write_Stream,
                Feedback_Token_Stream,
                X
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_HbmWriter_Stream
#endif
                )
    ;
}
