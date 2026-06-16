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

#ifdef JACOBI_TRACE_ENABLED
#define IF_JACOBI_TRACE_PAIR_ARGS(PAIR_ID) , Debug_Pair_Stream[PAIR_ID], PAIR_ID
#else
#define IF_JACOBI_TRACE_PAIR_ARGS(PAIR_ID)
#endif

#ifdef JACOBI_WIDE_HBM
#define JACOBI_INVOKE_UPDATE_PAIR(PAIR_ID) \
        .invoke(Jacobi_UpdatePairCompute, \
                Update_Pair_Command_Stream[PAIR_ID], \
                Vector_Y_Stream[(PAIR_ID) * JACOBI_ACC_GROUP_SIZE + 0], \
                Vector_Y_Stream[(PAIR_ID) * JACOBI_ACC_GROUP_SIZE + 1], \
                Vector_Y_Stream[(PAIR_ID) * JACOBI_ACC_GROUP_SIZE + 2], \
                Update_Coeff_Stream[PAIR_ID], \
                Update_Pair_Stream[PAIR_ID] \
                IF_JACOBI_TRACE_PAIR_ARGS(PAIR_ID) \
                )
#else
#define JACOBI_INVOKE_UPDATE_PAIR(PAIR_ID) \
        .invoke(Jacobi_UpdatePairCompute, \
                Update_Pair_Command_Stream[PAIR_ID], \
                Vector_Y_Stream[(PAIR_ID) * JACOBI_ACC_GROUP_SIZE + 0], \
                Vector_Y_Stream[(PAIR_ID) * JACOBI_ACC_GROUP_SIZE + 1], \
                Update_Coeff_Stream[PAIR_ID], \
                Update_Pair_Stream[PAIR_ID] \
                IF_JACOBI_TRACE_PAIR_ARGS(PAIR_ID) \
                )
#endif

// TAPA Jacobi iteration 实验顶层。
//
// host 侧先把 A 拆成 D+R，Jacobi_Vector_Loader 读 X 时取负，因此 Cuper service
// 输出的是 -R*x_old。update 后端直接完成：
//
//   x_next = (b + (-R*x_old)) * diag_inv
//
// 当前版本取消旧的 RoundToken/FeedbackToken 自循环和 UpdateFrameFork。
// Jacobi_MasterController 是唯一轮次推进者：每轮显式发矩阵/compute/update command，
// 等 XHbmWriter 的 done ack 后才进入下一轮；最后统一广播 stop。
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
    // 显式控制流一览。表里只列算法自己定义的 command/ack/stop/event；
    // TAPA stream 的 full/empty 反压和 AXI mmap 的 ready/valid 属于底层流控。
    //
    // 连接表：
    // 名字                                      作用                                      源                                      目标
    // ----------------------------------------  ----------------------------------------  --------------------------------------  -----------------------------------------
    // Command_Stream[0]                         启动/停止 SpElement ptr loader            Jacobi_MasterController                 SpmvService_SpElementPtrLoader
    // Command_Stream[1]                         启动/停止 X loader                        Jacobi_MasterController                 Jacobi_Vector_Loader
    // Matrix_Command_Stream[0..N-1]             启动/停止 N 路矩阵 loader                 Jacobi_MasterController                 SpmvService_MatrixLoader[0..N-1]
    // PE_Param[0..N]                            Core 串接参数流，内嵌 stop token          SpmvService_SpElementPtrLoader/Core      SpmvService_Core[0..N-1]/DestroyInt
    // Vector_Y_Param[0..N-1]                    Accumulator 参数流，内嵌 stop token       SpmvService_Core[0..N-1]                 SpmvService_Accumulator[0..N-1]
    // Update_Coeff_Command_Stream               B/Diag_inv 读取命令                       Jacobi_MasterController                 Jacobi_UpdateCoeffLoader
    // Update_Pair_Command_Stream[0..7]          8 路 pair compute 命令                    Jacobi_MasterController                 Jacobi_UpdatePairCompute[0..7]
    // Update_Pack_Command_Stream                更新拼包命令                              Jacobi_MasterController                 Jacobi_UpdatePackWriter
    // Update_Hbm_Command_Stream                 X HBM 写回命令                            Jacobi_MasterController                 Jacobi_XHbmWriter
    // Update_Done_Stream                        当前轮 X write response 已收齐            Jacobi_XHbmWriter                        Jacobi_MasterController
    // X_Write_Stream                            x_next 写回 FIFO                          Jacobi_UpdatePackWriter                  Jacobi_XHbmWriter
    // Vector_Destroy_Stop_Stream                Vector_X 链尾 drain 独立停止令牌          Jacobi_MasterController                 SpmvService_DestroyFloatV16
    // Stage_Event_Stream                        计时事件，不参与数学控制                  Jacobi_MasterController                 Jacobi_Stage_Timer
    // Stage_Ticks_Stream                        计时结果回传                              Jacobi_Stage_Timer                       Jacobi_MasterController
    //
    // 时序表：
    // 名字                                      发出时对应完成了啥事                                      接收时要做什么
    // ----------------------------------------  --------------------------------------------------------  ------------------------------------------------------------
    // Command_Stream[0]                         controller 开始本轮或最终收尾                            读取边界表并写 PE_Param[0]，或向 PE_Param 写 stop token
    // Command_Stream[1]                         controller 开始本轮，上一轮 X 写响应已收齐               非空 R 时读 X 并取负；空 R 时不读 X；stop 时退出
    // Matrix_Command_Stream[0..N-1]             controller 开始本轮或最终收尾                            从 Matrix_data 读 R 到 Matrix_A_Stream FIFO，或退出
    // Update_*_Command_Stream                   controller 已发出本轮 SpMV command                       启动本轮系数读取、pair update、拼包和 HBM 写回；stop 时退出
    // Update_Done_Stream                        XHbmWriter 已收齐本轮所有 HBM write response             controller 结束本轮计时并进入下一轮或广播 stop
    // X_Write_Stream                            一个 x_next float_v16 包已经拼好                         按地址顺序写入单 X buffer；FIFO 只解耦反压，不改变轮次边界
    // Vector_Destroy_Stop_Stream                controller 已发送所有计算 stop                           链尾 drain 等已知 X 包数 drain 完后退出
    // Stage_Event_Stream                        每轮开始/写回完成或最终 stop                             统计 cycle；stop 后输出计数
    // Stage_Ticks_Stream                        timer 收到 stop 并完成 cycle 汇总                        controller 读取后写 Metrics[4..7]

    // Command_Stream[0] 给 SpElement ptr loader，Command_Stream[1] 给 X vector loader。
    tapa::streams<CuperSpmvServiceCommand, 2, 4>               Command_Stream("Command_Stream");
    // HBM_CHANNEL_NUM 路矩阵 loader 各有一条 command stream；主控制器每轮显式发一次。
    tapa::streams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM, 4> Matrix_Command_Stream("Matrix_Command_Stream");

    // PE_Param 和 Vector_X_Stream 是 HBM_CHANNEL_NUM 级 Core 串接链；最后一项是链尾 drain。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>        PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>         Vector_X_Stream("Vector_X_Stream");
    // Matrix_A_Stream 是矩阵 FIFO。Core 只有收到 PE 参数后才消费这里的数据；
    // FIFO 满时 matrix loader 自然反压，避免无限预取。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 1024>         Matrix_A_Stream("Matrix_A_Stream");

    // Core 输出局部乘积到对应 Accumulator；Accumulator 输出 float_v2 直接进入 Jacobi update。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>             Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>         Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>              Vector_Y_Stream("Vector_Y_Stream");

    tapa::stream<JacobiUpdateCommand, 2>                       Update_Coeff_Command_Stream("Update_Coeff_Command_Stream");
    tapa::streams<JacobiUpdateCommand, JACOBI_UPDATE_PAIR_NUM, 2> Update_Pair_Command_Stream("Update_Pair_Command_Stream");
    tapa::stream<JacobiUpdateCommand, 2>                       Update_Pack_Command_Stream("Update_Pack_Command_Stream");
    tapa::stream<JacobiUpdateCommand, 2>                       Update_Hbm_Command_Stream("Update_Hbm_Command_Stream");
    tapa::stream<JacobiUpdateDone, 2>                          Update_Done_Stream("Update_Done_Stream");

    // Stage timer 独立统计真实 cycle，用 Metrics[4..7] 回传；由 controller 单路发事件。
    tapa::stream<JacobiStageEvent, 16>                         Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 8>                               Stage_Ticks_Stream("Stage_Ticks_Stream");

    // vector drain 没有内嵌 stop token，使用独立 stop stream。
    tapa::stream<INDEX_TYPE, 2>                                Vector_Destroy_Stop_Stream("Vector_Destroy_Stop_Stream");
    tapa::streams<JacobiCoeffPair, JACOBI_UPDATE_PAIR_NUM, FIFO_DEPTH> Update_Coeff_Stream("Update_Coeff_Stream");
    tapa::streams<JacobiUpdatedPair, JACOBI_UPDATE_PAIR_NUM, FIFO_DEPTH> Update_Pair_Stream("Update_Pair_Stream");
    // 写回 FIFO 解耦 update 拼包和 AXI HBM 写响应抖动；下一轮由 controller 在
    // Update_Done_Stream 到达后启动，保证单个 X buffer 不会读到半新半旧数据。
    tapa::stream<float_v16, 128>                               X_Write_Stream("X_Write_Stream");

#ifdef JACOBI_TRACE_ENABLED
    // trace/debug 只在 JACOBI_TRACE_LIGHT、JACOBI_TRACE_ISOTOPE 或
    // JACOBI_DEADLOCK_DEBUG 打开时存在。业务 task 只 try_write 非阻塞事件，
    // DebugMonitor 汇总写 Debug HBM，避免 debug 通路制造新反压。
    tapa::stream<JacobiDebugEvent, 16>                            Debug_Controller_Stream("Debug_Controller_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_PtrLoader_Stream("Debug_PtrLoader_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_VectorLoader_Stream("Debug_VectorLoader_Stream");
#ifdef JACOBI_TRACE_FULL
    tapa::streams<JacobiDebugEvent, HBM_CHANNEL_NUM, 16>          Debug_MatrixLoader_Stream("Debug_MatrixLoader_Stream");
    tapa::streams<JacobiDebugEvent, HBM_CHANNEL_NUM, 16>          Debug_Accumulator_Stream("Debug_Accumulator_Stream");
#endif
    tapa::stream<JacobiDebugEvent, 16>                            Debug_CoeffLoader_Stream("Debug_CoeffLoader_Stream");
    tapa::streams<JacobiDebugEvent, kJacobiDebugPairStreamCount, 16> Debug_Pair_Stream("Debug_Pair_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_PackWriter_Stream("Debug_PackWriter_Stream");
    tapa::stream<JacobiDebugEvent, 16>                            Debug_HbmWriter_Stream("Debug_HbmWriter_Stream");
    tapa::stream<INDEX_TYPE, 2>                                   Debug_Stop_Stream("Debug_Stop_Stream");
#endif

    tapa::task()
        // 主控制器显式推进全部轮次：发命令、等 X 写回 ack、最后广播 stop 并写 Status/Metrics。
        .invoke(Jacobi_MasterController,
                Command_Stream,
                Matrix_Command_Stream,
                Vector_Destroy_Stop_Stream,
#ifdef JACOBI_TRACE_ENABLED
                Debug_Controller_Stream,
                Debug_Stop_Stream,
#endif
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Update_Coeff_Command_Stream,
                Update_Pack_Command_Stream,
                Update_Hbm_Command_Stream,
                Update_Pair_Command_Stream,
                Update_Done_Stream,
                Status,
                Metrics,
                Row_num,
                Max_iters,
                Tau)
        // 根据 controller 发出的事件统计分段 cycle，并把结果回传给 controller 写 Metrics。
        .invoke(Jacobi_Stage_Timer,
                Stage_Event_Stream,
                Stage_Ticks_Stream)
#ifdef JACOBI_TRACE_ENABLED
        // 非阻塞 debug monitor。它定期写 heartbeat，并记录最后一次收到的事件。
        .invoke(Jacobi_DebugMonitor,
                Debug_Controller_Stream,
                Debug_PtrLoader_Stream,
                Debug_VectorLoader_Stream,
#ifdef JACOBI_TRACE_FULL
                Debug_MatrixLoader_Stream,
                Debug_Accumulator_Stream,
#endif
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
        // HBM_CHANNEL_NUM 路矩阵 loader 按 controller command 从各自 HBM channel 拉取矩阵数据到 FIFO。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoader,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_A_Stream
                                             ,
                                             tapa::seq()
#ifdef JACOBI_TRACE_FULL
                                             ,
                                             Debug_MatrixLoader_Stream
#endif
                                             )
        // Core 串接：PE_Param 和 Vector_X_Stream 逐级转发，Matrix_A_Stream 每级独占。
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
#ifdef JACOBI_WIDE_HBM
        // JACOBI_WIDE_HBM 宏下额外增加 Core16..Core23，对应 Matrix_data_16..23。
        .invoke(SpmvService_Core, PE_Param[16], Matrix_A_Stream[16], Vector_X_Stream[16], PE_Param[17], Vector_X_Stream[17], Vector_Y_Param[16], Matrix_Mult_Vector_Stream[16])
        .invoke(SpmvService_Core, PE_Param[17], Matrix_A_Stream[17], Vector_X_Stream[17], PE_Param[18], Vector_X_Stream[18], Vector_Y_Param[17], Matrix_Mult_Vector_Stream[17])
        .invoke(SpmvService_Core, PE_Param[18], Matrix_A_Stream[18], Vector_X_Stream[18], PE_Param[19], Vector_X_Stream[19], Vector_Y_Param[18], Matrix_Mult_Vector_Stream[18])
        .invoke(SpmvService_Core, PE_Param[19], Matrix_A_Stream[19], Vector_X_Stream[19], PE_Param[20], Vector_X_Stream[20], Vector_Y_Param[19], Matrix_Mult_Vector_Stream[19])
        .invoke(SpmvService_Core, PE_Param[20], Matrix_A_Stream[20], Vector_X_Stream[20], PE_Param[21], Vector_X_Stream[21], Vector_Y_Param[20], Matrix_Mult_Vector_Stream[20])
        .invoke(SpmvService_Core, PE_Param[21], Matrix_A_Stream[21], Vector_X_Stream[21], PE_Param[22], Vector_X_Stream[22], Vector_Y_Param[21], Matrix_Mult_Vector_Stream[21])
        .invoke(SpmvService_Core, PE_Param[22], Matrix_A_Stream[22], Vector_X_Stream[22], PE_Param[23], Vector_X_Stream[23], Vector_Y_Param[22], Matrix_Mult_Vector_Stream[22])
        .invoke(SpmvService_Core, PE_Param[23], Matrix_A_Stream[23], Vector_X_Stream[23], PE_Param[24], Vector_X_Stream[24], Vector_Y_Param[23], Matrix_Mult_Vector_Stream[23])
#endif
        // 消费 PE 参数链尾残余，保证上游 Core 的参数转发不会悬空阻塞。
        .invoke(SpmvService_DestroyInt, PE_Param[HBM_CHANNEL_NUM])
        // 消费 -X 广播链尾残余。drain 完所有应到达链尾的 X 包后再接受 stop。
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
        // 按 controller 命令读取 B 和 Diag_inv，为每个 row pair 提供 Jacobi 更新系数。
        .invoke(Jacobi_UpdateCoeffLoader,
                Update_Coeff_Command_Stream,
                Update_Coeff_Stream,
                B,
                Diag_inv
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_CoeffLoader_Stream
#endif
                )
        // 8 个 pair compute 各自消费 JACOBI_ACC_GROUP_SIZE 路 accumulator 输出；
        // 默认 16 HBM 时是 2 路，JACOBI_WIDE_HBM=1 时是 3 路。有效位置做
        // Jacobi 更新，padding 位置只读掉 -Rx，不再单独经过 checker 对齐。
        JACOBI_INVOKE_UPDATE_PAIR(0)
        JACOBI_INVOKE_UPDATE_PAIR(1)
        JACOBI_INVOKE_UPDATE_PAIR(2)
        JACOBI_INVOKE_UPDATE_PAIR(3)
        JACOBI_INVOKE_UPDATE_PAIR(4)
        JACOBI_INVOKE_UPDATE_PAIR(5)
        JACOBI_INVOKE_UPDATE_PAIR(6)
        JACOBI_INVOKE_UPDATE_PAIR(7)
        // pack writer 收齐 8 路 float_v2 并拼成 float_v16，先写入 X_Write_Stream。
        .invoke(Jacobi_UpdatePackWriter,
                Update_Pack_Command_Stream,
                Update_Pair_Stream,
                X_Write_Stream
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_PackWriter_Stream
#endif
                )
        // HBM writer 从 X_Write_Stream 连续写 X；write response 收齐后向 controller 发 done ack。
        .invoke(Jacobi_XHbmWriter,
                Update_Hbm_Command_Stream,
                X_Write_Stream,
                Update_Done_Stream,
                X
#ifdef JACOBI_TRACE_ENABLED
                ,
                Debug_HbmWriter_Stream
#endif
                )
    ;
}
