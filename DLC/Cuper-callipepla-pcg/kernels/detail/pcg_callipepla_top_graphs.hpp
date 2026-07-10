#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_controller.hpp"
#include "pcg_callipepla_spmv_output.hpp"
#include "pcg_callipepla_probe.hpp"
#include "pcg_callipepla_stage_timer.hpp"
#include "pcg_callipepla_vector_loader.hpp"
#include "pcg_callipepla_vector_phases.hpp"
#include "spmv_service_tasks.hpp"

static_assert(HBM_CHANNEL_NUM == 16,
              "CuperPcgCallipepla ABI currently fixes Matrix_data[0..15].");

#ifdef JACOBI_SPMV_STRIP_PADDING
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
#define CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID) , Debug_Core_Stream[CORE_ID]
#else
#define CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID)
#endif
#define CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(CORE_ID) \
        .invoke(SpmvService_CoreStrip, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                CORE_ID \
                CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID))
#else
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
#define CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID) , Debug_Core_Stream[CORE_ID]
#else
#define CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID)
#endif
#define CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(CORE_ID) \
        .invoke(SpmvService_Core, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                CORE_ID \
                CUPER_CALLIPEPLA_TRACE_CORE_ARG(CORE_ID))
#endif

void CuperPcgCallipepla(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                        tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                        tapa::mmap<double_v8> X_0,
                        tapa::mmap<double_v8> X_1,
                        tapa::mmap<double_v8> P_0,
                        tapa::mmap<double_v8> P_1,
                        tapa::mmap<float_v16> AP,
                        tapa::mmap<double_v8> R_0,
                        tapa::mmap<double_v8> R_1,
                        tapa::mmap<double_v8> M_inv,
                        tapa::mmap<double> Residuals,
                        tapa::mmap<INDEX_TYPE> Status,
                        tapa::mmap<double> Metrics,
                        const INDEX_TYPE Batch_num,
                        const INDEX_TYPE Matrix_len,
                        const INDEX_TYPE Row_num,
                        const INDEX_TYPE Column_num,
                        const INDEX_TYPE Max_iters,
                        const double Tau) {
#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 1
    tapa::task()
        .invoke(PcgCallipepla_Probe_TouchIndex,
                SpElement_list_ptr)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(PcgCallipepla_Probe_TouchMatrix,
                                             Matrix_data,
                                             tapa::seq())
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_1)
        .invoke(PcgCallipepla_Probe_TouchFloatV16,
                AP)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                M_inv)
        .invoke(PcgCallipepla_Probe_EntryTask,
                Residuals,
                Status,
                Metrics,
                Batch_num,
                Matrix_len,
                Row_num,
                Column_num,
                Max_iters,
                Tau)
    ;
#elif defined(CUPER_CALLIPEPLA_PROBE_ENABLED)
    tapa::stream<CuperSpmvServiceCommand, 4> Ptr_Command_Stream("Ptr_Command_Stream");
    tapa::streams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM, 4> Matrix_Command_Stream("Matrix_Command_Stream");
    tapa::stream<PcgCallipeplaSpmvVectorCommand, 4> Spmv_Vector_Command_Stream("Spmv_Vector_Command_Stream");
    tapa::stream<PcgCallipeplaVectorCommand, 8> Vector_Command_Stream("Vector_Command_Stream");
    tapa::stream<PcgCallipeplaVectorResult, 8> Vector_Result_Stream("Vector_Result_Stream");
    tapa::streams<INDEX_TYPE, 8, 2> Checker_Stop_Stream("Checker_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2> Sort_Stop_Stream("Sort_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2> Vector_Destroy_Rounds_Stream("Vector_Destroy_Rounds_Stream");
    tapa::stream<PcgCallipeplaStageEvent, 32> Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 16> Stage_Ticks_Stream("Stage_Ticks_Stream");

#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
    tapa::stream<PcgCallipeplaProbeEvent, 32> Controller_Probe_Event_Stream(
        "Controller_Probe_Event_Stream");
    tapa::stream<PcgCallipeplaProbeEvent, 16> Ack_Probe_Event_Stream(
        "Ack_Probe_Event_Stream");
#endif

#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 3
    tapa::stream<INDEX_TYPE, 128> PE_Param_Probe("PE_Param_Probe");
#ifdef JACOBI_SPMV_STRIP_PADDING
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 2> Matrix_Len_Stream("Matrix_Len_Stream");
#endif
#if CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL >= 2
    tapa::stream<float_v16, 256> Vector_X_Probe_Stream("Vector_X_Probe_Stream");
#endif
#endif

    tapa::task()
        .invoke(PcgCallipepla_Controller,
                Ptr_Command_Stream,
                Matrix_Command_Stream,
                Spmv_Vector_Command_Stream,
                Checker_Stop_Stream,
                Sort_Stop_Stream,
                Vector_Destroy_Rounds_Stream,
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Vector_Command_Stream,
                Vector_Result_Stream,
#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
                Controller_Probe_Event_Stream,
#else
                Status,
#endif
                Residuals,
                Metrics,
                Batch_num,
                Matrix_len,
                Row_num,
                Column_num,
                Max_iters,
                Tau)
        .invoke(PcgCallipepla_Stage_Timer,
                Stage_Event_Stream,
                Stage_Ticks_Stream)
        .invoke(PcgCallipepla_Probe_VectorPhaseAck,
                Vector_Command_Stream,
                Vector_Result_Stream
#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
                ,
                Ack_Probe_Event_Stream)
        .invoke(PcgCallipepla_Probe_HandshakeMonitor,
                Controller_Probe_Event_Stream,
                Ack_Probe_Event_Stream,
                Status,
                Matrix_len,
                Row_num)
#else
                )
#endif
#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
        .invoke(PcgCallipepla_Probe_TouchIndex,
                SpElement_list_ptr)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(PcgCallipepla_Probe_TouchMatrix,
                                             Matrix_data,
                                             tapa::seq())
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_1)
        .invoke(PcgCallipepla_Probe_TouchFloatV16,
                AP)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                M_inv)
#elif CUPER_CALLIPEPLA_PROBE_MODE_ID == 3
#if CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL < 2
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                X_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                P_1)
#endif
        .invoke(PcgCallipepla_Probe_TouchFloatV16,
                AP)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_0)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                R_1)
        .invoke(PcgCallipepla_Probe_TouchDoubleV8,
                M_inv)
#endif
#if CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
        .invoke(PcgCallipepla_Probe_CommandDrain,
                Ptr_Command_Stream,
                0)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(PcgCallipepla_Probe_CommandDrain,
                                             Matrix_Command_Stream,
                                             tapa::seq())
        .invoke(PcgCallipepla_Probe_SpmvVectorCommandDrain,
                Spmv_Vector_Command_Stream)
        .invoke(PcgCallipepla_Probe_ScalarStopDrain,
                Vector_Destroy_Rounds_Stream)
#elif CUPER_CALLIPEPLA_PROBE_MODE_ID == 3
#ifdef JACOBI_SPMV_STRIP_PADDING
        .invoke(SpmvService_StripPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param_Probe,
                Matrix_Len_Stream)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(PcgCallipepla_Probe_MatrixLoaderStripDrain,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_Len_Stream,
                                             tapa::seq())
#else
        .invoke(SpmvService_SpElementPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param_Probe)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(PcgCallipepla_Probe_MatrixLoaderDrain,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             tapa::seq())
#endif
        .invoke(PcgCallipepla_Probe_PEParamDrain,
                PE_Param_Probe)
#if CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL >= 2
        .invoke(PcgCallipepla_Vector_Loader,
                Batch_num,
                Column_num,
                X_0,
                X_1,
                P_0,
                P_1,
                Spmv_Vector_Command_Stream,
                Vector_X_Probe_Stream)
        .invoke(PcgCallipepla_DestroyFloatV16,
                Batch_num,
                Column_num,
                Vector_X_Probe_Stream,
                Vector_Destroy_Rounds_Stream)
#else
        .invoke(PcgCallipepla_Probe_SpmvVectorCommandDrain,
                Spmv_Vector_Command_Stream)
        .invoke(PcgCallipepla_Probe_ScalarStopDrain,
                Vector_Destroy_Rounds_Stream)
#endif
#endif
        .invoke<tapa::join, 8>(PcgCallipepla_Probe_StopDrain,
                               Checker_Stop_Stream,
                               tapa::seq())
        .invoke(PcgCallipepla_Probe_ScalarStopDrain,
                Sort_Stop_Stream)
    ;
#else
    tapa::stream<CuperSpmvServiceCommand, 4> Ptr_Command_Stream("Ptr_Command_Stream");
    tapa::streams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM, 4> Matrix_Command_Stream("Matrix_Command_Stream");
    tapa::stream<PcgCallipeplaSpmvVectorCommand, 4> Spmv_Vector_Command_Stream("Spmv_Vector_Command_Stream");

    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128> PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256> Vector_X_Stream("Vector_X_Stream");
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 1024> Matrix_A_Stream("Matrix_A_Stream");
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64> Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256> Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
#ifdef JACOBI_SPMV_STRIP_PADDING
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 2> Matrix_Len_Stream("Matrix_Len_Stream");
#endif
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256> Vector_Y_Stream("Vector_Y_Stream");
    tapa::streams<float_v2, 8, FIFO_DEPTH> Vector_Y_Stream_Aftck("Vector_Y_Stream_Aftck");
    tapa::stream<float_v16, 128> Spmv_Result_Stream("Spmv_Result_Stream");

    tapa::stream<PcgCallipeplaVectorCommand, 8> Vector_Command_Stream("Vector_Command_Stream");
    tapa::stream<PcgCallipeplaVectorResult, 8> Vector_Result_Stream("Vector_Result_Stream");
    tapa::streams<INDEX_TYPE, 8, 2> Checker_Stop_Stream("Checker_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2> Sort_Stop_Stream("Sort_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2> Vector_Destroy_Rounds_Stream("Vector_Destroy_Rounds_Stream");

    tapa::stream<PcgCallipeplaStageEvent, 32> Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 16> Stage_Ticks_Stream("Stage_Ticks_Stream");

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    tapa::stream<PcgCallipeplaStatusWrite, 64> Status_Write_Stream("Status_Write_Stream");
    tapa::stream<PcgCallipeplaDebugEvent, 16> Debug_Controller_Stream("Debug_Controller_Stream");
    tapa::stream<PcgCallipeplaDebugEvent, 16> Debug_PtrLoader_Stream("Debug_PtrLoader_Stream");
    tapa::stream<PcgCallipeplaDebugEvent, 16> Debug_VectorLoader_Stream("Debug_VectorLoader_Stream");
    tapa::streams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM, 8> Debug_MatrixLoader_Stream("Debug_MatrixLoader_Stream");
    tapa::streams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM, 8> Debug_Core_Stream("Debug_Core_Stream");
    tapa::streams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM, 8> Debug_Accumulator_Stream("Debug_Accumulator_Stream");
    tapa::streams<PcgCallipeplaDebugEvent, 8, 8> Debug_Checker_Stream("Debug_Checker_Stream");
    tapa::stream<PcgCallipeplaDebugEvent, 16> Debug_SortTree_Stream("Debug_SortTree_Stream");
    tapa::stream<PcgCallipeplaDebugEvent, 16> Debug_VectorPhases_Stream("Debug_VectorPhases_Stream");
    tapa::stream<INDEX_TYPE, 2> Debug_Stop_Stream("Debug_Stop_Stream");
#endif

    tapa::task()
        .invoke(PcgCallipepla_Controller,
                Ptr_Command_Stream,
                Matrix_Command_Stream,
                Spmv_Vector_Command_Stream,
                Checker_Stop_Stream,
                Sort_Stop_Stream,
                Vector_Destroy_Rounds_Stream,
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Vector_Command_Stream,
                Vector_Result_Stream,
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                Status_Write_Stream,
                Debug_Controller_Stream,
                Debug_Stop_Stream,
#else
                Status,
#endif
                Residuals,
                Metrics,
                Batch_num,
                Matrix_len,
                Row_num,
                Column_num,
                Max_iters,
                Tau)
        .invoke(PcgCallipepla_Stage_Timer,
                Stage_Event_Stream,
                Stage_Ticks_Stream)
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        .invoke(PcgCallipepla_StatusMonitor,
                Status_Write_Stream,
                Debug_Controller_Stream,
                Debug_PtrLoader_Stream,
                Debug_VectorLoader_Stream,
                Debug_MatrixLoader_Stream,
                Debug_Core_Stream,
                Debug_Accumulator_Stream,
                Debug_Checker_Stream,
                Debug_SortTree_Stream,
                Debug_VectorPhases_Stream,
                Debug_Stop_Stream,
                Status)
#endif
        .invoke(PcgCallipepla_Vector_Loader,
                Batch_num,
                Column_num,
                X_0,
                X_1,
                P_0,
                P_1,
                Spmv_Vector_Command_Stream,
                Vector_X_Stream[0]
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                ,
                Debug_VectorLoader_Stream
#endif
                )
        .invoke(PcgCallipepla_Vector_Phases,
                Vector_Command_Stream,
                Vector_Result_Stream,
                Spmv_Result_Stream,
                X_0,
                X_1,
                P_0,
                P_1,
                AP,
                R_0,
                R_1,
                M_inv,
                Row_num
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                ,
                Debug_VectorPhases_Stream
#endif
                )
#ifdef JACOBI_SPMV_STRIP_PADDING
        .invoke(SpmvService_StripPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param[0],
                Matrix_Len_Stream
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                ,
                Debug_PtrLoader_Stream
#endif
                )
#else
        .invoke(SpmvService_SpElementPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param[0]
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                ,
                Debug_PtrLoader_Stream
#endif
                )
#endif
#ifdef JACOBI_SPMV_STRIP_PADDING
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoaderStrip,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_Len_Stream,
                                             Matrix_A_Stream,
                                             tapa::seq()
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                                             ,
                                             Debug_MatrixLoader_Stream
#endif
                                             )
#else
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoader,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_A_Stream,
                                             tapa::seq()
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                                             ,
                                             Debug_MatrixLoader_Stream
#endif
                                             )
#endif
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(0)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(1)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(2)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(3)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(4)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(5)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(6)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(7)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(8)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(9)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(10)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(11)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(12)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(13)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(14)
        CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(15)
        .invoke(PcgCallipepla_DestroyInt, PE_Param[HBM_CHANNEL_NUM])
        .invoke(PcgCallipepla_DestroyFloatV16,
                Batch_num,
                Column_num,
                Vector_X_Stream[HBM_CHANNEL_NUM],
                Vector_Destroy_Rounds_Stream)
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_Accumulator,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream,
                                             tapa::seq()
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                                             ,
                                             Debug_Accumulator_Stream
#endif
                                             )
        .invoke<tapa::join, 8>(PcgCallipepla_Vector_Checker,
                               Row_num,
                               Vector_Y_Stream,
                               Vector_Y_Stream_Aftck,
                               Checker_Stop_Stream,
                               tapa::seq()
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                               ,
                               Debug_Checker_Stream
#endif
                               )
        .invoke(PcgCallipepla_Mult_Sort_Tree,
                Vector_Y_Stream_Aftck,
                Spmv_Result_Stream,
                Sort_Stop_Stream
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                ,
                Debug_SortTree_Stream
#endif
                )
    ;
#endif
}

#undef CUPER_CALLIPEPLA_INVOKE_SPMV_CORE
#undef CUPER_CALLIPEPLA_TRACE_CORE_ARG
