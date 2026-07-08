#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_controller.hpp"
#include "pcg_callipepla_spmv_output.hpp"
#include "pcg_callipepla_stage_timer.hpp"
#include "pcg_callipepla_vector_loader.hpp"
#include "pcg_callipepla_vector_phases.hpp"
#include "spmv_service_tasks.hpp"

static_assert(HBM_CHANNEL_NUM == 16,
              "CuperPcgCallipepla ABI currently fixes Matrix_data[0..15].");

#ifdef JACOBI_SPMV_STRIP_PADDING
#define CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(CORE_ID) \
        .invoke(SpmvService_CoreStrip, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                CORE_ID)
#else
#define CUPER_CALLIPEPLA_INVOKE_SPMV_CORE(CORE_ID) \
        .invoke(SpmvService_Core, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID])
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
                Residuals,
                Status,
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
        .invoke(PcgCallipepla_Vector_Loader,
                Batch_num,
                Column_num,
                X_0,
                X_1,
                P_0,
                P_1,
                Spmv_Vector_Command_Stream,
                Vector_X_Stream[0])
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
                Row_num)
#ifdef JACOBI_SPMV_STRIP_PADDING
        .invoke(SpmvService_StripPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param[0],
                Matrix_Len_Stream)
#else
        .invoke(SpmvService_SpElementPtrLoader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Ptr_Command_Stream,
                PE_Param[0])
#endif
#ifdef JACOBI_SPMV_STRIP_PADDING
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoaderStrip,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_Len_Stream,
                                             Matrix_A_Stream,
                                             tapa::seq())
#else
        .invoke<tapa::join, HBM_CHANNEL_NUM>(SpmvService_MatrixLoader,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_Command_Stream,
                                             Matrix_A_Stream,
                                             tapa::seq())
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
                                             tapa::seq())
        .invoke<tapa::join, 8>(PcgCallipepla_Vector_Checker,
                               Row_num,
                               Vector_Y_Stream,
                               Vector_Y_Stream_Aftck,
                               Checker_Stop_Stream)
        .invoke(PcgCallipepla_Mult_Sort_Tree,
                Vector_Y_Stream_Aftck,
                Spmv_Result_Stream,
                Sort_Stop_Stream)
    ;
}

#undef CUPER_CALLIPEPLA_INVOKE_SPMV_CORE
