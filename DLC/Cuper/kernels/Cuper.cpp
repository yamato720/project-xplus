#include <ap_int.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <tapa.h>

#include "Cuper.h"

using namespace std;

// TAPA Cuper 共享 kernel 文件。
//
// Project-XPlus 当前只把这里的两个顶层作为四条 Cuper 主线的一部分：
//   1. Cuper    ：TAPA Cuper / single SpMV，host 可选择 spmv-only 或兼容 host-PCG。
//   2. CuperPcg ：TAPA Cuper / FPGA-PCG，PCG 控制进入 TAPA task graph。
//
// 下面大量 loader/core/accumulator/checker task 是两条 TAPA 路线共用的
// 16-HBM Cuper SpMV 流水，入口处再决定是否只跑 SpMV，还是由 controller
// 在 FPGA 内反复驱动 SpMV 并完成 PCG 迭代。
struct Matrix_Mult_X {
    ap_uint<18> row[8];
    float_v8 val;
};

template <typename T1, typename T2>
inline void Async_Read(tapa::async_mmap<T1> &A,
                       tapa::ostream<T1> &fifo_A,
                       const T2 A_len,
                       T2 &i_request,
                       T2 &i_response
                      ) {

#pragma HLS inline
    if((i_request < A_len) && !A.read_addr.full()) {
        A.read_addr.try_write(i_request);
        ++i_request;
    }
    if(!fifo_A.full() && !A.read_data.empty()) {
        T1 temp;
        A.read_data.try_read(temp);
        fifo_A.try_write(temp);
        ++i_response;
    }
}

void SpElement_list_ptr_Loader(const INDEX_TYPE Batch_num,
                               const INDEX_TYPE Row_num,
                               const INDEX_TYPE Iteration_num,
                               const INDEX_TYPE Column_num,
                               tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                               tapa::ostream<INDEX_TYPE> &PE_Param
                              ) {

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param.write(Batch_num);
    PE_Param.write(Row_num);
    PE_Param.write(Iteration_num);
    PE_Param.write(Column_num);
    
    const INDEX_TYPE Batch_num_plus_1 = Batch_num + 1;
iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    read_ptr:
        for(INDEX_TYPE i_request = 0, i_response = 0; i_response < Batch_num_plus_1;) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            Async_Read(SpElement_list_ptr,
                       PE_Param,
                       Batch_num_plus_1,
                       i_request, 
                       i_response
                      );
        }
    }
}

void Vector_Loader(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Column_num,
                   tapa::async_mmap<float_v16> &X, 
                   tapa::ostream<float_v16> &Vector_X_Stream
                  ) {

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE Batch_num_X    = ((Column_num + 15) >> 4);

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    Loader_X:
        for(INDEX_TYPE i_request = 0, i_response = 0; i_response < Batch_num_X;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            Async_Read(X,
                       Vector_X_Stream,
                       Batch_num_X,
                       i_request, 
                       i_response
                      );
        }
    }
}

void Matrix_Loader(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Matrix_len,
                   tapa::async_mmap<ap_uint<512>> &Matrix_data,
                   tapa::ostream<ap_uint<512>> &Matrix_A_Stream
                  ) {

     const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    Load_A:
        for(INDEX_TYPE i_request = 0, i_response = 0; i_response < Matrix_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
            Async_Read(Matrix_data,
                       Matrix_A_Stream,
                       Matrix_len,
                       i_request, 
                       i_response
                      );
        }
    }
}

#ifdef X_TABLE
inline bool Comparator(INDEX_TYPE Data_a,                                           
                       INDEX_TYPE Data_b                                      
                      ) {
#pragma HLS inline
    return Data_a >= Data_b ? true : false;
}
#endif

void Core(tapa::istream<INDEX_TYPE>    &PE_Param_in,
          tapa::istream<ap_uint<512> >  &Matrix_A_Stream,
          tapa::istream<float_v16>     &Vector_X_Stream_in,
          tapa::ostream<INDEX_TYPE>    &PE_Param_out,
          tapa::ostream<float_v16>     &Vector_X_Stream_out,
          tapa::ostream<INDEX_TYPE>    &Vector_Y_Param,
          tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream
         ) {

    const INDEX_TYPE Batch_num     = PE_Param_in.read();
    const INDEX_TYPE Row_num       = PE_Param_in.read();
    const INDEX_TYPE Iteration_num = PE_Param_in.read();
    const INDEX_TYPE Column_num    = PE_Param_in.read();

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param_out.write(Batch_num);
    PE_Param_out.write(Row_num);
    PE_Param_out.write(Iteration_num);
    PE_Param_out.write(Column_num);
    
    Vector_Y_Param.write(Batch_num);
    Vector_Y_Param.write(Row_num);
    Vector_Y_Param.write(Iteration_num);

#ifdef X_TABLE   
    INDEX_TYPE x_table_min[X_TABLE_DEPTH];
    INDEX_TYPE x_table_max[X_TABLE_DEPTH];

#pragma HLS bind_storage variable=x_table_min latency=1
#pragma HLS bind_storage variable=x_table_max latency=1

    for(INDEX_TYPE iter = 0; iter < X_TABLE_ITERATION_NUM; ++iter) {

        INDEX_TYPE start_32 = PE_Param_in.read();
        PE_Param_out.write(start_32);
        Vector_Y_Param.write(start_32);
        
        for(INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49

            const INDEX_TYPE end_32 = PE_Param_in.read();
            PE_Param_out.write(end_32);
            Vector_Y_Param.write(end_32);

            for(INDEX_TYPE j = 0; (j < Slice_WIDTH_DIV_16) && (j < ((Column_num + 15) >> 4) - i * Slice_WIDTH_DIV_16); ) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II = 1
                if(!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                    float_v16 x;
                    Vector_X_Stream_in.try_read(x);
                    Vector_X_Stream_out.try_write(x);
                    ++j;
                }
            }

            INDEX_TYPE x_table_min_a = 8193;
            INDEX_TYPE x_table_max_a = -1;

            for(INDEX_TYPE j = start_32; j < end_32; ) {
                if(!Matrix_A_Stream.empty()) {

                    ap_uint<512> spelement;
                    Matrix_A_Stream.try_read(spelement);
                    Matrix_Mult_X matmultx;

                    for(INDEX_TYPE p = 0; p < 8; ++p) {
                        ap_uint<64> a     = spelement(63 + p * 64, p * 64);
                        ap_uint<14> a_col = a(63, 50);
                        ap_uint<18> a_row = a(49, 32);

                        matmultx.row[p] = a_row;

                        if(a_row[17] == 0) {
                            if(iter == 0) {
                                if(Comparator(x_table_min_a, a_col))
                                    x_table_min_a = a_col;
                                if(Comparator(a_col, x_table_max_a))
                                    x_table_max_a = a_col;
                            }
                        }
                    }
                    Matrix_Mult_Vector_Stream.write(matmultx);
                    ++j;
                }
            }
            start_32 = end_32;
            if(iter == 0) {
                x_table_min[i] = x_table_min_a;
                x_table_max[i] = x_table_max_a;
            }
        }
    } 

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time - X_TABLE_ITERATION_NUM; ++iter) {
#endif

#ifndef X_TABLE
iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#endif

#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        
        VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

        INDEX_TYPE start_32 = PE_Param_in.read();
        PE_Param_out.write(start_32);
        Vector_Y_Param.write(start_32);
        
    main:
        for(INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49

#ifdef X_TABLE
        INDEX_TYPE x_table_start = x_table_min[i];
        INDEX_TYPE x_table_end   = x_table_max[i];
#endif
        const INDEX_TYPE total_vector_packets = (Column_num + 15) >> 4;
        const INDEX_TYPE start_idx = i * Slice_WIDTH_DIV_16;
        const INDEX_TYPE end_idx = min(start_idx + Slice_WIDTH_DIV_16,
                                       total_vector_packets);
        
        Load_vector:
            for(INDEX_TYPE j = start_idx; j < end_idx; ) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II = 1
                if(!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                    float_v16 x;
                    Vector_X_Stream_in.try_read(x);
                    Vector_X_Stream_out.try_write(x);

                    for(INDEX_TYPE k = 0; k < 16; ++k) {
                        for(INDEX_TYPE l = 0; l < X_BRAM_DEPTH; ++l) {
                            local_X[l][((j - start_idx) << 4) + k] = x[k];
                        }
                    }
                    ++j;
                }
            }

            const INDEX_TYPE end_32 = PE_Param_in.read();
            PE_Param_out.write(end_32);
            Vector_Y_Param.write(end_32);
        
            for(INDEX_TYPE j = start_32; j < end_32; ) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
                if(!Matrix_A_Stream.empty()) {
                Decoder:
                    ap_uint<512> spelement;
                    Matrix_A_Stream.try_read(spelement);
                    Matrix_Mult_X matmultx;

#ifdef FLEX_REUSE
                    ap_uint<14> col_old = 0x3FFF;
                    VALUE_TYPE val_old = 0.0;
#endif
                    for(INDEX_TYPE p = 0; p < 8; ++p) {
                        ap_uint<64> a     = spelement(63 + p * 64, p * 64);
                        ap_uint<14> a_col = a(63, 50);
                        ap_uint<18> a_row = a(49, 32);
                        ap_uint<32> a_val = a(31,  0);
                        
                        matmultx.row[p] = a_row;
                    PE:
                        if(a_row[17] == 0) {
#ifdef FLEX_REUSE
                            VALUE_TYPE val;
                            if((col_old & a_col) == 0x3FFF) {
                                val = val_old;
                            }
                            else {
                                val = tapa::bit_cast<VALUE_TYPE>(a_val);
                            }
#else
                            VALUE_TYPE val = tapa::bit_cast<VALUE_TYPE>(a_val);
#endif
                            matmultx.val[p] = val * local_X[p / (8 / X_BRAM_DEPTH)][a_col];
#ifdef FLEX_REUSE
                            col_old = a_col;
                            val_old = val;
#endif
                        }
                    }
                    Matrix_Mult_Vector_Stream.write(matmultx);
                    ++j;
                }
            }
            start_32 = end_32;
        }
    }
}

inline void Adder(ap_uint<18> addr,
                  VALUE_TYPE  val_new,
                  ap_uint<64> local_part_Y[URAM_DEPTH]
                 ) {

#pragma HLS inline

    ap_uint<64> part_val_u64     = local_part_Y[addr(17, 1)];
    ap_uint<32> part_val_d0_u32  = part_val_u64(31,  0);
    ap_uint<32> part_val_d1_u32  = part_val_u64(63, 32);
    ap_uint<32> part_val_u32     = (addr[0]) ? part_val_d1_u32 : part_val_d0_u32;
    
    VALUE_TYPE part_val_plus_new = tapa::bit_cast<VALUE_TYPE>(part_val_u32) + val_new;
   
    part_val_u32 = tapa::bit_cast<ap_uint<32>>(part_val_plus_new);
    
    if(addr[0]) {
        part_val_d1_u32 = part_val_u32;
    } 
    else {
        part_val_d0_u32 = part_val_u32;
    }
    
    part_val_u64(63, 32)        = part_val_d1_u32;
    part_val_u64(31,  0)        = part_val_d0_u32;
    local_part_Y[addr(17, 1)]   = part_val_u64;
}

inline void Adder_p(ap_uint<17> addr,
                    VALUE_TYPE  val_new,
                    ap_uint<32> local_part_Y[URAM_DEPTH]
                   ) {

#pragma HLS inline
    ap_uint<32> part_val_u32     = local_part_Y[addr];                                      
        
    VALUE_TYPE part_val_plus_new = tapa::bit_cast<VALUE_TYPE>(part_val_u32) + val_new;  
    part_val_u32 = tapa::bit_cast<ap_uint<32> >(part_val_plus_new);                      
        
    local_part_Y[addr]   = part_val_u32;
}

void Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                 tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                 tapa::ostream<float_v2>      &Vector_Y_Stream
                ) {
    
    const INDEX_TYPE Batch_num      = Vector_Y_Param.read();
    const INDEX_TYPE Row_num        = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_num  = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    
    const INDEX_TYPE num_v_init     = (Row_num + HBM_CHANNEL_NUM_MULT_16 - 1) / HBM_CHANNEL_NUM_MULT_16;
    const INDEX_TYPE num_v_out      = (Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2;
    


#ifdef PINGPONG
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];

#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1

    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1

#else

    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH];

#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1

#endif

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        
    local_part_Y:
        for(int i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            for(int p = 0; p < 8; ++p) {
                local_part_Y_ping[p][i] = 0;
#ifdef PINGPONG
                local_part_Y_pong[p][i] = 0;
#endif
            }
        }
        
        INDEX_TYPE start_32 = Vector_Y_Param.read();
        
    main:
        for(int i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
            
        const INDEX_TYPE end_32 = Vector_Y_Param.read();

        accumulate:
            for(INDEX_TYPE j = start_32; j < end_32; ) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=local_part_Y_ping distance=WINDOWS
#ifdef PINGPONG
#pragma HLS dependence true variable=local_part_Y_pong distance=WINDOWS
#endif
                if(!Matrix_Mult_Vector_Stream.empty()) {
                    Matrix_Mult_X matmultx; 
                    Matrix_Mult_Vector_Stream.try_read(matmultx);

                    for(int p = 0; p < 8; ++p) {
                        ap_uint<18> a_row = matmultx.row[p];
#ifdef PINGPONG 
                        if(a_row[17] == 0 && a_row[0] == 0)
                            Adder_p(a_row(17, 1),
                                    matmultx.val[p],
                                    local_part_Y_ping[p]
                                   );
                        if(a_row[17] == 0 && a_row[0] == 1)
                            Adder_p(a_row(17, 1),
                                    matmultx.val[p],
                                    local_part_Y_pong[p]
                                   );

#else
                        if(a_row[17] == 0) 
                            Adder(a_row,
                                  matmultx.val[p],
                                  local_part_Y_ping[p]
                                 );
#endif
                    }
                    ++j;
                }
            }
            start_32 = end_32;
        }


    writer:
        for(INDEX_TYPE i = 0, c_idx = 0; i < num_v_out; ++i) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            float_v2 out_v;
#ifdef PINGPONG
            ap_uint<32> u_32_0 = local_part_Y_ping[c_idx][i >> 3];
            ap_uint<32> u_32_1 = local_part_Y_pong[c_idx][i >> 3];
            out_v[0] = tapa::bit_cast<VALUE_TYPE>(u_32_0);
            out_v[1] = tapa::bit_cast<VALUE_TYPE>(u_32_1);
#else
            ap_uint<64> u_64 = local_part_Y_ping[c_idx][i >> 3];
            for(INDEX_TYPE d = 0; d < 2; ++d) {
                ap_uint<32> u_32_d = u_64(31 + 32 * d, 32 * d);
                out_v[d] = tapa::bit_cast<VALUE_TYPE>(u_32_d);
            }
#endif
            Vector_Y_Stream.write(out_v);
            ++c_idx;
            if(c_idx == 8) {
                c_idx = 0;
            }
        }
    }
}

void Vector_Checker(const INDEX_TYPE Iteration_num,
                    const INDEX_TYPE Row_num,
                    tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                    tapa::ostream<float_v2> &Vector_Y_Stream_Aftck
                   ) {

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_pe_output = ((Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2) * HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE num_out = (Row_num + 15) >> 4;
    const INDEX_TYPE num_ite_Y = num_pe_output * Iteration_time;
out:
    for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_ite_Y;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
        if (!Vector_Y_Stream[c_idx].empty() && !Vector_Y_Stream_Aftck.full()) {
            float_v2 tmp; 
            Vector_Y_Stream[c_idx].try_read(tmp);
            if(o_idx < num_out) {
                Vector_Y_Stream_Aftck.try_write(tmp);
            }
            ++i;
            ++c_idx;
            ++o_idx;
            if(c_idx == HBM_CHANNEL_NUM_DIV_8) {
                c_idx = 0;
            }
            if(o_idx == num_pe_output) {
                o_idx = 0;
            }
        }
    }
}
void Mult_Sort_Tree(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                    tapa::ostream<float_v16>    &Vector_Y_Stream_Ans
                   ) {
    for(;;) {
#pragma HLS pipeline II=1
        bool all_ready = true;
        for(int i = 0; i < 8; ++i) {
            if(Vector_Y_Stream_Aftck[i].empty()) {
                all_ready = false;
                break;
            }
        }

        if(all_ready && !Vector_Y_Stream_Ans.full()) {
            float_v16 tmpv16;
            for(int i = 0; i < 8; ++i) {
                float_v2 val;
                Vector_Y_Stream_Aftck[i].try_read(val); 
                
                tmpv16[(i << 1)]     = val[0]; 
                tmpv16[(i << 1) + 1] = val[1]; 
            }
            Vector_Y_Stream_Ans.try_write(tmpv16);
        }
    }
}
void Vector_Writer(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Row_num,
                   tapa::istream<float_v16> &Vector_Y_Stream_Ans,
                   tapa::async_mmap<float_v16> &Y_out
                  ) {
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_ite_Y = (Row_num + 15) >> 4;
    
iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    write_Y:
        for(INDEX_TYPE i_request = 0, i_response = 0; i_response < num_ite_Y;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            if((i_request < num_ite_Y) && !Vector_Y_Stream_Ans.empty() && !Y_out.write_addr.full() && !Y_out.write_data.full() ) {
                Y_out.write_addr.try_write(i_request);
                float_v16 tmpv16;
                Vector_Y_Stream_Ans.try_read(tmpv16);
                Y_out.write_data.try_write(tmpv16);
                ++i_request;
            }
            uint8_t n_resp;
            if(Y_out.write_resp.try_read(n_resp)) {
                i_response += int(n_resp) + 1;
            }
        }
    }

}

struct CuperSpmvCommand {
    // 传给 Cuper SpMV 服务流水的一次运行命令。
    // 在 CuperPcg 中每次 PCG 需要 A*x0 或 A*p 时，controller 都发送一次。
    INDEX_TYPE iteration_num;
    INDEX_TYPE stop;
};

struct PcgStageEvent {
    INDEX_TYPE stage;
    INDEX_TYPE op;
};

static constexpr INDEX_TYPE kPcgStatusConverged = 0;
static constexpr INDEX_TYPE kPcgStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgStatusBreakdown = 2;
static constexpr INDEX_TYPE kPcgStopToken = -1;
static constexpr INDEX_TYPE kPcgStageBegin = 0;
static constexpr INDEX_TYPE kPcgStageEnd = 1;
static constexpr INDEX_TYPE kPcgStageStop = 2;
static constexpr INDEX_TYPE kPcgStageInitSpmv = 0;
static constexpr INDEX_TYPE kPcgStageInitZp = 1;
static constexpr INDEX_TYPE kPcgStageIterSpmv = 2;
static constexpr INDEX_TYPE kPcgStageUpdateXr = 3;
static constexpr INDEX_TYPE kPcgStageUpdateZ = 4;
static constexpr INDEX_TYPE kPcgStageUpdateP = 5;
static constexpr INDEX_TYPE kPcgStageControllerTotal = 6;
static constexpr INDEX_TYPE kPcgStageCount = 7;
static constexpr double kPcgBreakdownEps = 1.0e-30;

inline double pcg_abs(const double value) {
#pragma HLS inline
    return value < 0.0 ? -value : value;
}

inline bool pcg_invalid(const double value) {
#pragma HLS inline
    return value != value;
}

inline void pcg_stage_mark(tapa::ostream<PcgStageEvent> &Stage_Event_out,
                           const INDEX_TYPE stage,
                           const INDEX_TYPE op) {
#pragma HLS inline
    PcgStageEvent event;
    event.stage = stage;
    event.op = op;
    Stage_Event_out.write(event);
}

// PCG 版的 SpElement ptr loader。
//
// 原始 Cuper 顶层只启动一次，所以 loader 读固定参数后顺序跑完。
// CuperPcg 里 SpMV 会被 PCG controller 多次触发，因此 loader 作为
// 常驻服务任务，收到一条 CuperSpmvCommand 就向 PE_Param 重新广播
// Batch/Row/Iteration/Column 和每个 batch 的 SpElement 边界。
void Pcg_SpElement_list_ptr_Loader(const INDEX_TYPE Batch_num,
                                   const INDEX_TYPE Row_num,
                                   const INDEX_TYPE Column_num,
                                   tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                                   tapa::istream<CuperSpmvCommand> &Command_in,
                                   tapa::ostream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            PE_Param.write(kPcgStopToken);
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

        PE_Param.write(Batch_num);
        PE_Param.write(Row_num);
        PE_Param.write(command.iteration_num);
        PE_Param.write(Column_num);

        const INDEX_TYPE batch_num_plus_1 = Batch_num + 1;
    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        read_ptr:
            for (INDEX_TYPE i_request = 0, i_response = 0; i_response < batch_num_plus_1;) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
                Async_Read(SpElement_list_ptr,
                           PE_Param,
                           batch_num_plus_1,
                           i_request,
                           i_response);
            }
        }
    }
}

// PCG 版向量 loader。
//
// X_in 不是 HBM mmap，而是 controller 写入的流。这样 controller 可以在
// 同一个 kernel 内把当前 x0 或每轮 p 打包成 float_v16，直接喂给 Cuper
// SpMV 流水，避免 host 每轮重新 launch Cuper。
void Pcg_Vector_Loader(const INDEX_TYPE Column_num,
                       tapa::istream<CuperSpmvCommand> &Command_in,
                       tapa::istream<float_v16> &X_in,
                       tapa::ostream<float_v16> &Vector_X_Stream) {
    const INDEX_TYPE batch_num_x = ((Column_num + 15) >> 4);

    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        loader_x:
            for (INDEX_TYPE i = 0; i < batch_num_x;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
                if (!X_in.empty() && !Vector_X_Stream.full()) {
                    float_v16 x;
                    X_in.try_read(x);
                    Vector_X_Stream.try_write(x);
                    ++i;
                }
            }
        }
    }
}

// 16 路矩阵 HBM loader 的 PCG 服务版。
//
// 每个 HBM channel 一个实例。收到 controller 发来的命令后，从对应
// Matrix_data[channel] 顺序读 Matrix_len 个 512-bit word，保持原 Cuper
// 的 16 通道矩阵吞吐。
void Pcg_Matrix_Loader(const INDEX_TYPE Matrix_len,
                       tapa::async_mmap<ap_uint<512>> &Matrix_data,
                       tapa::istream<CuperSpmvCommand> &Command_in,
                       tapa::ostream<ap_uint<512>> &Matrix_A_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        load_a:
            for (INDEX_TYPE i_request = 0, i_response = 0; i_response < Matrix_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
                Async_Read(Matrix_data,
                           Matrix_A_Stream,
                           Matrix_len,
                           i_request,
                           i_response);
            }
        }
    }
}

// Cuper Core 的常驻服务版。
//
// 计算逻辑保持原 Cuper Core 的结构：每个 channel 载入当前 slice 的
// x 本地缓存，解码 512-bit SpElement 包，完成 8 lane 乘法并输出
// Matrix_Mult_X 给 accumulator。区别是外层 for(;;) 允许 PCG controller
// 多次触发 SpMV。
void Pcg_Core(tapa::istream<INDEX_TYPE>    &PE_Param_in,
              tapa::istream<ap_uint<512> >  &Matrix_A_Stream,
              tapa::istream<float_v16>     &Vector_X_Stream_in,
              tapa::ostream<INDEX_TYPE>    &PE_Param_out,
              tapa::ostream<float_v16>     &Vector_X_Stream_out,
              tapa::ostream<INDEX_TYPE>    &Vector_Y_Param,
              tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        const INDEX_TYPE Batch_num = PE_Param_in.read();
        if (Batch_num == kPcgStopToken) {
            PE_Param_out.write(kPcgStopToken);
            Vector_Y_Param.write(kPcgStopToken);
            return;
        }
        const INDEX_TYPE Row_num = PE_Param_in.read();
        const INDEX_TYPE Iteration_num = PE_Param_in.read();
        const INDEX_TYPE Column_num = PE_Param_in.read();

        const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

        PE_Param_out.write(Batch_num);
        PE_Param_out.write(Row_num);
        PE_Param_out.write(Iteration_num);
        PE_Param_out.write(Column_num);

        Vector_Y_Param.write(Batch_num);
        Vector_Y_Param.write(Row_num);
        Vector_Y_Param.write(Iteration_num);

    iter:
        for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
            VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

            INDEX_TYPE start_32 = PE_Param_in.read();
            PE_Param_out.write(start_32);
            Vector_Y_Param.write(start_32);

        main:
            for (INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
                const INDEX_TYPE total_vector_packets = (Column_num + 15) >> 4;
                const INDEX_TYPE start_idx = i * Slice_WIDTH_DIV_16;
                const INDEX_TYPE end_idx = min(start_idx + Slice_WIDTH_DIV_16,
                                               total_vector_packets);

            load_vector:
                for (INDEX_TYPE j = start_idx; j < end_idx;) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II=1
                    if (!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                        float_v16 x;
                        Vector_X_Stream_in.try_read(x);
                        Vector_X_Stream_out.try_write(x);

                        for (INDEX_TYPE k = 0; k < 16; ++k) {
                            for (INDEX_TYPE l = 0; l < X_BRAM_DEPTH; ++l) {
                                local_X[l][((j - start_idx) << 4) + k] = x[k];
                            }
                        }
                        ++j;
                    }
                }

                const INDEX_TYPE end_32 = PE_Param_in.read();
                PE_Param_out.write(end_32);
                Vector_Y_Param.write(end_32);

            decode:
                for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
                    if (!Matrix_A_Stream.empty()) {
                        ap_uint<512> spelement;
                        Matrix_A_Stream.try_read(spelement);
                        Matrix_Mult_X matmultx;

#ifdef FLEX_REUSE
                        ap_uint<14> col_old = 0x3FFF;
                        VALUE_TYPE val_old = 0.0;
#endif
                        for (INDEX_TYPE p = 0; p < 8; ++p) {
                            ap_uint<64> a = spelement(63 + p * 64, p * 64);
                            ap_uint<14> a_col = a(63, 50);
                            ap_uint<18> a_row = a(49, 32);
                            ap_uint<32> a_val = a(31, 0);

                            matmultx.row[p] = a_row;
                            if (a_row[17] == 0) {
#ifdef FLEX_REUSE
                                VALUE_TYPE val;
                                if ((col_old & a_col) == 0x3FFF) {
                                    val = val_old;
                                } else {
                                    val = tapa::bit_cast<VALUE_TYPE>(a_val);
                                }
#else
                                VALUE_TYPE val = tapa::bit_cast<VALUE_TYPE>(a_val);
#endif
                                matmultx.val[p] =
                                    val * local_X[p / (8 / X_BRAM_DEPTH)][a_col];
#ifdef FLEX_REUSE
                                col_old = a_col;
                                val_old = val;
#endif
                            }
                        }
                        Matrix_Mult_Vector_Stream.write(matmultx);
                        ++j;
                    }
                }
                start_32 = end_32;
            }
        }
    }
}

// Cuper accumulator 的常驻服务版。
//
// 多个 Core 输出的是按物理 PE 分散的部分和；这里用 URAM 累加成每行
// y 值，再按 Cuper 原输出顺序吐出 float_v2。Pcg_Vector_Checker 和
// Mult_Sort_Tree 后续会重新拼成 float_v16。
void Pcg_Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                     tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                     tapa::ostream<float_v2>      &Vector_Y_Stream) {
#ifdef PINGPONG
    // ping/pong 分别保存偶数行和奇数行的部分和。row 编码来自 host 侧
    // Reordering：bit0 表示奇偶，bit[17:1] 是局部累加地址，bit17=1
    // 表示空元素。这里不是按原始全局 row 直接索引。
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1
#else
    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
#endif

    for (;;) {
#pragma HLS loop_flatten off
        const INDEX_TYPE Batch_num = Vector_Y_Param.read();
        if (Batch_num == kPcgStopToken) {
            return;
        }
        const INDEX_TYPE Row_num = Vector_Y_Param.read();
        const INDEX_TYPE Iteration_num = Vector_Y_Param.read();
        const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
        const INDEX_TYPE num_v_init =
            (Row_num + HBM_CHANNEL_NUM_MULT_16 - 1) / HBM_CHANNEL_NUM_MULT_16;
        const INDEX_TYPE num_v_out =
            (Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2;

    iter:
        for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        local_part_Y:
            for (int i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
                for (int p = 0; p < 8; ++p) {
                    local_part_Y_ping[p][i] = 0;
#ifdef PINGPONG
                    local_part_Y_pong[p][i] = 0;
#endif
                }
            }

            INDEX_TYPE start_32 = Vector_Y_Param.read();

        main:
            for (int i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
                const INDEX_TYPE end_32 = Vector_Y_Param.read();

            accumulate:
                for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=local_part_Y_ping distance=WINDOWS
#ifdef PINGPONG
#pragma HLS dependence true variable=local_part_Y_pong distance=WINDOWS
#endif
                    if (!Matrix_Mult_Vector_Stream.empty()) {
                        Matrix_Mult_X matmultx;
                        Matrix_Mult_Vector_Stream.try_read(matmultx);

                        for (int p = 0; p < 8; ++p) {
                            ap_uint<18> a_row = matmultx.row[p];
#ifdef PINGPONG
                            // a_row[17] 为 1 是 padding/空元素；有效元素按
                            // a_row[0] 分流到 ping/pong，地址使用 a_row(17,1)。
                            // 所以这里的 18-bit row 不是 0..Row_num-1 的全局行号。
                            if (a_row[17] == 0 && a_row[0] == 0)
                                Adder_p(a_row(17, 1), matmultx.val[p], local_part_Y_ping[p]);
                            if (a_row[17] == 0 && a_row[0] == 1)
                                Adder_p(a_row(17, 1), matmultx.val[p], local_part_Y_pong[p]);
#else
                            if (a_row[17] == 0)
                                Adder(a_row, matmultx.val[p], local_part_Y_ping[p]);
#endif
                        }
                        ++j;
                    }
                }
                start_32 = end_32;
            }

        writer:
            for (INDEX_TYPE i = 0, c_idx = 0; i < num_v_out; ++i) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
                float_v2 out_v;
#ifdef PINGPONG
                ap_uint<32> u_32_0 = local_part_Y_ping[c_idx][i >> 3];
                ap_uint<32> u_32_1 = local_part_Y_pong[c_idx][i >> 3];
                out_v[0] = tapa::bit_cast<VALUE_TYPE>(u_32_0);
                out_v[1] = tapa::bit_cast<VALUE_TYPE>(u_32_1);
#else
                ap_uint<64> u_64 = local_part_Y_ping[c_idx][i >> 3];
                for (INDEX_TYPE d = 0; d < 2; ++d) {
                    ap_uint<32> u_32_d = u_64(31 + 32 * d, 32 * d);
                    out_v[d] = tapa::bit_cast<VALUE_TYPE>(u_32_d);
                }
#endif
                Vector_Y_Stream.write(out_v);
                ++c_idx;
                if (c_idx == 8) {
                    c_idx = 0;
                }
            }
        }
    }
}

// 过滤 Cuper accumulator 的补齐输出。
//
// Cuper 内部按 HBM/PE 对齐输出，真实 Row_num 末尾可能不足一个完整包。
// checker 只保留有效范围内的 float_v2，避免 controller 消费到 padding。
void Pcg_Vector_Checker(const INDEX_TYPE Row_num,
                        tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                        tapa::ostream<float_v2> &Vector_Y_Stream_Aftck,
                        tapa::istream<INDEX_TYPE> &Stop_in) {
    const INDEX_TYPE num_pe_output =
        ((Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2) *
        HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE num_out = (Row_num + 15) >> 4;

    for (;;) {
#pragma HLS loop_flatten off
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }
    out:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            if (!Stop_in.empty()) {
                INDEX_TYPE stop;
                Stop_in.try_read(stop);
                return;
            }
            if (!Vector_Y_Stream[c_idx].empty() && !Vector_Y_Stream_Aftck.full()) {
                float_v2 tmp;
                Vector_Y_Stream[c_idx].try_read(tmp);
                if (o_idx < num_out) {
                    Vector_Y_Stream_Aftck.try_write(tmp);
                }
                ++i;
                ++c_idx;
                ++o_idx;
                if (c_idx == HBM_CHANNEL_NUM_DIV_8) {
                    c_idx = 0;
                }
                if (o_idx == num_pe_output) {
                    o_idx = 0;
                }
            }
        }
    }
}

void Pcg_Mult_Sort_Tree(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                        tapa::ostream<float_v16> &Vector_Y_Stream_Ans,
                        tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }

        bool all_ready = true;
        for (int i = 0; i < 8; ++i) {
            if (Vector_Y_Stream_Aftck[i].empty()) {
                all_ready = false;
                break;
            }
        }

        if (all_ready && !Vector_Y_Stream_Ans.full()) {
            float_v16 tmpv16;
            for (int i = 0; i < 8; ++i) {
                float_v2 val;
                Vector_Y_Stream_Aftck[i].try_read(val);

                tmpv16[(i << 1)]     = val[0];
                tmpv16[(i << 1) + 1] = val[1];
            }
            Vector_Y_Stream_Ans.try_write(tmpv16);
        }
    }
}

void Pcg_Stage_Timer(tapa::istream<PcgStageEvent> &Stage_Event_in,
                     tapa::ostream<ap_uint<64>> &Stage_Ticks_out) {
    ap_uint<64> now = 0;
    ap_uint<64> start[kPcgStageCount];
    ap_uint<64> elapsed[kPcgStageCount];
#pragma HLS array_partition variable=start complete
#pragma HLS array_partition variable=elapsed complete

init_stage_timer_arrays:
    for (INDEX_TYPE index = 0; index < kPcgStageCount; ++index) {
#pragma HLS unroll
        start[index] = 0;
        elapsed[index] = 0;
    }

stage_timer_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++now;
        if (!Stage_Event_in.empty()) {
            PcgStageEvent event;
            Stage_Event_in.try_read(event);
            if (event.op == kPcgStageStop) {
                break;
            }
            if (event.stage >= 0 && event.stage < kPcgStageCount) {
                if (event.op == kPcgStageBegin) {
                    start[event.stage] = now;
                } else if (event.op == kPcgStageEnd) {
                    elapsed[event.stage] += now - start[event.stage];
                }
            }
        }
    }

write_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kPcgStageCount; ++index) {
#pragma HLS pipeline II=1
        Stage_Ticks_out.write(elapsed[index]);
    }
    Stage_Ticks_out.write(now);
}

// FPGA 内 PCG 主控。
//
// 这是 CuperPcg 和 host-PCG 旧版的核心区别：
//   1. 初始化阶段发起 A*x0，计算 r=b-A*x0、z=M_inv*r、p=z。
//   2. 每轮发起 A*p，计算 p_ap、alpha，然后更新 x/r/z。
//   3. 计算 beta 并更新 p，直到 rr<=Tau、达到 Max_iters 或 breakdown。
//
// SpMV 本身仍由下面的 TAPA Cuper 数据流服务完成；controller 只负责
// 发送命令、提供 x/p 向量、消费 SpMV 结果和维护 PCG 向量状态。
void Pcg_Controller(tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
                    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
                    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
                    tapa::ostream<PcgStageEvent> &Stage_Event_out,
                    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
                    tapa::ostream<float_v16> &X_to_spmv,
                    tapa::istream<float_v16> &Spmv_in,
                    tapa::mmap<double> B,
                    tapa::mmap<double> M_inv,
                    tapa::mmap<double> X,
                    tapa::mmap<double> R,
                    tapa::mmap<double> Z,
                    tapa::mmap<double> P,
                    tapa::mmap<double> AP,
                    tapa::mmap<double> Metrics,
                    tapa::mmap<INDEX_TYPE> Status,
                    const INDEX_TYPE Row_num,
                    const INDEX_TYPE Max_iters,
                    const double Tau) {
    // controller 和 SpMV 数据流之间用 float_v16 作为向量包。
    // packet_count 只描述 PCG 向量长度，和 Cuper 内部 18-bit row 编码
    // 不是一回事。
    const INDEX_TYPE packet_count = (Row_num + 15) >> 4;
    INDEX_TYPE status_code = kPcgStatusMaxIter;
    INDEX_TYPE iterations = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    unsigned long long init_spmv_ticks = 0;
    unsigned long long init_zp_ticks = 0;
    unsigned long long iter_spmv_ticks = 0;
    unsigned long long update_xr_ticks = 0;
    unsigned long long update_z_ticks = 0;
    unsigned long long update_p_ticks = 0;
    CuperSpmvCommand command;
    command.iteration_num = 1;
    command.stop = 0;
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageBegin);

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
// 初始化 SpMV：先用当前 X 计算 A*x0。
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageBegin);
send_init_command:
        for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        Command_out[index].write(command);
    }
send_init_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(command);
    }

    init_spmv_stream:
        // 边发送 x0 边消费 A*x0，避免 SpMV 输出 FIFO 填满后反压整条
        // Cuper 数据流，而 controller 仍阻塞在继续写输入向量。
        // 这个循环故意使用 full/empty + try_write/try_read 做非阻塞握手：
        // 大矩阵时输入向量和输出结果会同时在流水里移动，不能拆成
        // “先全部写完，再全部读完”的两段。
        for (INDEX_TYPE sent_packets = 0, received_packets = 0;
             received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
            ++init_spmv_ticks;
            if (sent_packets < packet_count && !X_to_spmv.full()) {
                float_v16 x_packet;
        fill_x0_packet:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                    const INDEX_TYPE index = (sent_packets << 4) + lane;
                    double value = 0.0;
                    if (index < Row_num) {
                        value = X[index];
                    }
                    x_packet[lane] = static_cast<VALUE_TYPE>(value);
                }
                X_to_spmv.try_write(x_packet);
                ++sent_packets;
            }

            if (!Spmv_in.empty()) {
                const INDEX_TYPE packet = received_packets;
                float_v16 ap_packet;
                Spmv_in.try_read(ap_packet);
        init_r_lanes:
                // 第一段只消费 A*x0 并生成初始残差 R。上一版 init_vectors 同时读 B/M_inv、
                // 写 R/Z/P、做 rz/rr 归约，route 最后 4 根冲突线集中在这条大流水。
                // 拆开后用一次额外 R 读取换取更小的局部 FP64/HBM 访问压力。
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=4
                    const INDEX_TYPE index = (packet << 4) + lane;
                    if (index < Row_num) {
                        const double b_value = B[index];
                        const double ap_value = static_cast<double>(ap_packet[lane]);
                        const double r_value = b_value - ap_value;
                        R[index] = r_value;
                    }
                }
                ++received_packets;
            }
        }
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageEnd);

        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageBegin);
init_zp_reduce:
        // 第二段再读 R/M_inv，初始化 Z/P 并累计 rz/rr。它和 update_z_reduce
        // 形态接近，避免把 residual 初始化和 SpMV 输出消费挤在同一条流水里。
        for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
            const double r_value = R[index];
            const double minv_value = M_inv[index];
            const double z_value = minv_value * r_value;
            Z[index] = z_value;
            P[index] = z_value;
            rz += r_value * z_value;
            rr += r_value * r_value;
        }
        init_zp_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageEnd);

pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
    // 每轮 SpMV：将当前搜索方向 p 送入 Cuper 流水，计算 AP=A*p。
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageBegin);
    send_iter_command:
            for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
                Command_out[index].write(command);
            }
    send_iter_matrix_command:
            for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
                Matrix_Command_out[index].write(command);
            }

            p_ap = 0.0;
    iter_spmv_stream:
            // 每轮同样边发送 p 边消费 A*p。否则大矩阵时 controller 可能在
            // X_to_spmv.write() 等下游腾空间，而下游又在等 controller 读取
            // 已经算出的 SpMV 输出，形成硬件死锁。
            // sent_packets 和 received_packets 独立推进，允许 Cuper SpMV
            // 先产出部分 AP，也允许 controller 继续补发后续 p packet。
            for (INDEX_TYPE sent_packets = 0, received_packets = 0;
                 received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                ++iter_spmv_ticks;
                if (sent_packets < packet_count && !X_to_spmv.full()) {
                    float_v16 p_packet;
            fill_p_packet:
                    for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                        const INDEX_TYPE index = (sent_packets << 4) + lane;
                        double value = 0.0;
                        if (index < Row_num) {
                            value = P[index];
                        }
                        p_packet[lane] = static_cast<VALUE_TYPE>(value);
                    }
                    X_to_spmv.try_write(p_packet);
                    ++sent_packets;
                }

                if (!Spmv_in.empty()) {
                    const INDEX_TYPE packet = received_packets;
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
            ap_lanes:
                    // 消费 AP，同时累计 p^T AP。AP 写回 HBM 便于调试和 host 检查。
                    for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                        const INDEX_TYPE index = (packet << 4) + lane;
                        if (index < Row_num) {
                            const double p_value = P[index];
                            const double ap_value = static_cast<double>(ap_packet[lane]);
                            AP[index] = ap_value;
                            p_ap += p_value * ap_value;
                        }
                    }
                    ++received_packets;
                }
            }
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageEnd);

            if (pcg_invalid(p_ap) || pcg_abs(p_ap) <= kPcgBreakdownEps ||
                pcg_invalid(rz) || pcg_abs(rz) <= kPcgBreakdownEps) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            alpha = rz / p_ap;
            if (pcg_invalid(alpha)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageBegin);
    update_xr:
            // 只更新 x/r。上一版把 x/r/z 更新、M_inv 读取、rz/rr 归约
            // 都塞在同一个 update_xrz pipeline 里，route 失败集中在该
            // pipeline 的 FP64 乘法和 AXI 读写附近。这里把它拆成两段，
            // 用一次额外 R 读取换取更小的局部布线热点。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
                const double x_value = X[index];
                const double p_value = P[index];
                const double r_value = R[index];
                const double ap_value = AP[index];
                const double x_new = x_value + alpha * p_value;
                const double r_new = r_value - alpha * ap_value;
                X[index] = x_new;
                R[index] = r_new;
            }
            update_xr_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageEnd);

            double rz_new = 0.0;
            double rr_new = 0.0;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageBegin);
    update_z_reduce:
            // 再更新 z 并累计新残差。该段只读 R/M_inv、写 Z，避免和
            // update_xr 的 X/P/AP 访问以及 alpha 乘法挤在同一条流水里。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
                const double r_new = R[index];
                const double minv_value = M_inv[index];
                const double z_new = minv_value * r_new;
                Z[index] = z_new;
                rz_new += r_new * z_new;
                rr_new += r_new * r_new;
            }
            update_z_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageEnd);

            if (pcg_invalid(rz_new) || pcg_invalid(rr_new)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            const double beta = rz_new / rz;
            if (pcg_invalid(beta)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateP, kPcgStageBegin);
    update_p:
            // p = z + beta * p。下一轮 controller 会重新把 P 打包送入 SpMV。
            // 同样放宽 II，避免 beta 更新路径和 update_xrz 争抢同一区域布线。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=2
                const double z_value = Z[index];
                const double p_value = P[index];
                P[index] = z_value + beta * p_value;
            }
            update_p_ticks += static_cast<unsigned long long>(Row_num) * 2ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateP, kPcgStageEnd);

            rz = rz_new;
            rr = rr_new;
            iterations = iter + 1;
        }

        if (status_code != kPcgStatusBreakdown) {
            status_code = (rr <= Tau) ? kPcgStatusConverged : kPcgStatusMaxIter;
        }
    }

    CuperSpmvCommand stop_command;
    stop_command.iteration_num = 0;
    stop_command.stop = 1;
send_stop_command:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        Command_out[index].write(stop_command);
    }
send_stop_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(stop_command);
    }
send_checker_stop:
    for (INDEX_TYPE index = 0; index < 8; ++index) {
#pragma HLS unroll
        Checker_Stop_out[index].write(1);
    }
    Sort_Stop_out.write(1);
    Vector_Destroy_Stop_out.write(1);
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageEnd);
    pcg_stage_mark(Stage_Event_out, 0, kPcgStageStop);

    ap_uint<64> stage_cycles[kPcgStageCount + 1];
#pragma HLS array_partition variable=stage_cycles complete
read_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kPcgStageCount + 1; ++index) {
#pragma HLS pipeline II=1
        stage_cycles[index] = Stage_Ticks_in.read();
    }

    // Metrics/Status 是 host 侧判断运行结果和调试数值稳定性的最小输出。
    Metrics[0] = rz;
    Metrics[1] = rr;
    Metrics[2] = p_ap;
    Metrics[3] = alpha;
    Metrics[4] = static_cast<double>(packet_count);
    Metrics[5] = static_cast<double>(init_spmv_ticks);
    Metrics[6] = static_cast<double>(init_zp_ticks);
    Metrics[7] = static_cast<double>(iter_spmv_ticks);
    Metrics[8] = static_cast<double>(update_xr_ticks);
    Metrics[9] = static_cast<double>(update_z_ticks);
    Metrics[10] = static_cast<double>(update_p_ticks);
    Metrics[11] = static_cast<double>(init_spmv_ticks + init_zp_ticks +
                                      iter_spmv_ticks + update_xr_ticks +
                                      update_z_ticks + update_p_ticks);
    Metrics[12] = static_cast<double>(Row_num);
    Metrics[13] = static_cast<double>(Max_iters);
    Metrics[16] = static_cast<double>(stage_cycles[kPcgStageInitSpmv].to_uint64());
    Metrics[17] = static_cast<double>(stage_cycles[kPcgStageInitZp].to_uint64());
    Metrics[18] = static_cast<double>(stage_cycles[kPcgStageIterSpmv].to_uint64());
    Metrics[19] = static_cast<double>(stage_cycles[kPcgStageUpdateXr].to_uint64());
    Metrics[20] = static_cast<double>(stage_cycles[kPcgStageUpdateZ].to_uint64());
    Metrics[21] = static_cast<double>(stage_cycles[kPcgStageUpdateP].to_uint64());
    Metrics[22] = static_cast<double>(stage_cycles[kPcgStageControllerTotal].to_uint64());
    Metrics[23] = static_cast<double>(stage_cycles[kPcgStageCount].to_uint64());
    Status[0] = status_code;
    Status[1] = iterations;
}

void Destroy_int(tapa::istream<INDEX_TYPE> &PE_Param) {
    for(;;) {
#pragma HLS pipeline II=1
        INDEX_TYPE tmp; 
        PE_Param.try_read(tmp);
    }
}

void Destroy_float_v16(tapa::istream<float_v16> &Vector_X_Stream) {
    for(;;) {
#pragma HLS pipeline II=1
        float_v16 tmp; 
        Vector_X_Stream.try_read(tmp);
    }
}

void Pcg_Destroy_int(tapa::istream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS pipeline II=1
        const INDEX_TYPE tmp = PE_Param.read();
        if (tmp == kPcgStopToken) {
            return;
        }
    }
}

void Pcg_Destroy_float_v16(tapa::istream<float_v16> &Vector_X_Stream,
                           tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }
        if (!Vector_X_Stream.empty()) {
            float_v16 tmp;
            Vector_X_Stream.try_read(tmp);
        }
    }
}

// TAPA Cuper 顶层 kernel：这里只实现 Cuper 风格 SpMV。
//
// 这个 kernel 的职责是：
//   X + Matrix_data_0..15 -> Y_out
// host 传入 Batch_num / Matrix_len / Row_num / Column_num / Iteration_num，
// kernel 内部通过 TAPA task graph 完成向量加载、16 路矩阵读取、Core 乘加、
// Accumulator、检查和写回。
//
// 注意：这里不是 PCG control-kernel。
// 这个顶层没有 r/z/p/ap/m_inv/alpha/beta/status 等 PCG 状态参数，
// 也不做 Jacobi-PCG 的收敛判断。Project-XPlus 的 Cuper-PCG TAPA 版
// 是在 host 侧执行 PCG 主循环，每轮把当前 p/x 向量送进这个 Cuper
// kernel 做一次 SpMV。
void Cuper(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
           tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
           tapa::mmap<float_v16> X,
           tapa::mmap<float_v16> Y_out,
           const INDEX_TYPE Batch_num,
           const INDEX_TYPE Matrix_len,
           const INDEX_TYPE Row_num,
           const INDEX_TYPE Column_num,
           const INDEX_TYPE Iteration_num
          ) {

    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>    PE_Param("PE_Param");                          
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 1024>     Vector_X_Stream("Vector_X_Stream");                            
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 512>      Matrix_A_Stream("Matrix_A_Stream");                  
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>         Vector_Y_Param("Vector_Y_Param");                            
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 1024>     Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");    
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 1024>          Vector_Y_Stream("Vector_Y_Stream");                      
    tapa::streams<float_v2, 8, FIFO_DEPTH>                  Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");                      
    
    tapa::task()
        .invoke(SpElement_list_ptr_Loader, Batch_num, Row_num, Iteration_num, Column_num, SpElement_list_ptr, PE_Param[0])
        .invoke(Vector_Loader, Iteration_num, Column_num, X, Vector_X_Stream[0])
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Matrix_Loader, Iteration_num, Matrix_len, Matrix_data, Matrix_A_Stream)
        .invoke(Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        .invoke(Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        .invoke(Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        .invoke(Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        .invoke(Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        .invoke(Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        .invoke(Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        .invoke(Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        .invoke(Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        .invoke(Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        .invoke(Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        .invoke(Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        .invoke(Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        .invoke(Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        .invoke(Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        .invoke(Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        .invoke<tapa::detach>(Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        .invoke<tapa::detach>(Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM])
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        .invoke<tapa::join, 8>(Vector_Checker, Iteration_num, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck)
        .invoke<tapa::detach>(Mult_Sort_Tree, Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans)
        .invoke(Vector_Writer, Iteration_num, Row_num, Vector_Y_Stream_Ans, Y_out)
    ;
}

void CuperPcg(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
              tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
              tapa::mmap<double> B,
              tapa::mmap<double> M_inv,
              tapa::mmap<double> X,
              tapa::mmap<double> R,
              tapa::mmap<double> Z,
              tapa::mmap<double> P,
              tapa::mmap<double> AP,
              tapa::mmap<double> Metrics,
              tapa::mmap<INDEX_TYPE> Status,
              const INDEX_TYPE Batch_num,
              const INDEX_TYPE Matrix_len,
              const INDEX_TYPE Row_num,
              const INDEX_TYPE Column_num,
              const INDEX_TYPE Max_iters,
              const double Tau
             ) {

    // CuperPcg 顶层数据流：
    //
    //   Pcg_Controller
    //       -> 发送 SpMV 命令到 ptr/vector/matrix loader
    //       -> 将 x0 或 p 打包成 float_v16 送入 Pcg_X_Stream
    //       <- 从 Pcg_Spmv_Stream 接收 A*x0 或 A*p
    //
    //   Pcg_* loader/Core/Accumulator/Checker/Mult_Sort_Tree
    //       -> 基本沿用原始 Cuper 的 16 HBM SpMV 流水
    //
    // 下面这些 stream 数组可以按硬件连线图理解：
    //
    //   1. 参数/向量广播链，长度是 HBM_CHANNEL_NUM + 1：
    //
    //        PE_Param[0]        -> Core0 -> PE_Param[1]
    //        PE_Param[1]        -> Core1 -> PE_Param[2]
    //        ...
    //        PE_Param[15]       -> Core15 -> PE_Param[16]
    //
    //        Vector_X_Stream[0] -> Core0 -> Vector_X_Stream[1]
    //        Vector_X_Stream[1] -> Core1 -> Vector_X_Stream[2]
    //        ...
    //        Vector_X_Stream[15]-> Core15 -> Vector_X_Stream[16]
    //
    //      x0/p 向量不是复制出 16 个独立输入端口，而是通过 16 个 core
    //      串接转发。每个 core 在转发同一份向量的同时，读取自己那一路
    //      HBM 矩阵并计算局部 val * x[col]。
    //
    //   2. 矩阵/局部乘积并行数组，长度是 HBM_CHANNEL_NUM：
    //
    //        Matrix_data_0  -> Matrix_A_Stream[0]  -> Core0  -> Matrix_Mult_Vector_Stream[0]
    //        Matrix_data_1  -> Matrix_A_Stream[1]  -> Core1  -> Matrix_Mult_Vector_Stream[1]
    //        ...
    //        Matrix_data_15 -> Matrix_A_Stream[15] -> Core15 -> Matrix_Mult_Vector_Stream[15]
    //
    //      这部分是真正的 16 路 HBM/SpMV 并行度。
    //
    //   3. SpMV 输出收敛链：
    //
    //        Matrix_Mult_Vector_Stream[0..15]
    //             -> Pcg_Accumulator[0..15]
    //             -> Vector_Y_Stream[0..15]
    //             -> Pcg_Vector_Checker[0..7]
    //             -> Vector_Y_Stream_Aftck[0..7]
    //             -> Mult_Sort_Tree
    //             -> Pcg_Spmv_Stream
    //             -> Pcg_Controller
    //
    //      controller 最终看到的是一包包 float_v16 的 A*x0 或 A*p。
    //
    // 这样 host 只 launch 一次 CuperPcg；PCG 每轮迭代都在这个 TAPA
    // task graph 内部完成，不再走 host 侧循环调用 Cuper。
    //
    // tapa::stream<T, DEPTH> 表示一条 FIFO；tapa::streams<T, N, DEPTH>
    // 表示 N 条同类型 FIFO。T 是每个元素的数据类型，DEPTH 是每条 FIFO
    // 的深度。下面这些 FIFO 就是各个 task 之间的硬件连线。
    //
    // 2 条命令流：controller 分别通知 ptr loader 和 vector loader
    // 启动一次 SpMV，结束时再发 stop 让服务任务退出。
    tapa::streams<CuperSpmvCommand, 2, 4>                   Command_Stream("Command_Stream");
    // 16 条矩阵命令流：controller 给每个 HBM matrix loader 发同一轮
    // SpMV 命令。HBM_CHANNEL_NUM 当前是 16。
    tapa::streams<CuperSpmvCommand, HBM_CHANNEL_NUM, 4>     Matrix_Command_Stream("Matrix_Command_Stream");
    // controller 输出的 x0/p 向量流。float_v16 是 16 个 float 一包，
    // 先进入 Pcg_Vector_Loader，再被广播到 16 个 core。
    tapa::stream<float_v16, 128>                            Pcg_X_Stream("Pcg_X_Stream");
    // 参数广播链：PE_Param[0] 由 ptr loader 写入，随后 Core0..Core15
    // 逐级转发到 PE_Param[16]。链尾由 Destroy_int 消费。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>     PE_Param("PE_Param");
    // 向量广播链：Vector_X_Stream[0] 由 vector loader 写入，随后
    // Core0..Core15 逐级转发到 Vector_X_Stream[16]。链尾由
    // Destroy_float_v16 消费。
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>      Vector_X_Stream("Vector_X_Stream");
    // 16 路矩阵数据流：每一路对应一个 HBM channel。ap_uint<512>
    // 是一个 512-bit HBM beat，内部打包 8 个 64-bit SpElement。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 64>        Matrix_A_Stream("Matrix_A_Stream");
    // 16 路 accumulator 参数流：每个 core 给对应 accumulator 传 Row_num、
    // Iteration_num 以及每个 batch 的矩阵边界。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>          Vector_Y_Param("Vector_Y_Param");
    // 16 路局部乘积流：每个 core 输出自己 HBM 分片产生的 val * x[col]
    // 及对应的 Cuper 内部 row 编码。
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>      Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    // 16 路 accumulator 输出流：每路输出 float_v2，也就是 ping/pong
    // 合并后的两行 y 值。
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>           Vector_Y_Stream("Vector_Y_Stream");
    // checker 后的 8 路输出流：过滤 padding 后交给 Mult_Sort_Tree，
    // 最终重新拼成 float_v16 送回 Pcg_Controller。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                  Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    // 直接把 Cuper 的 float_v16 SpMV 结果接回 controller。
    // 之前额外的 packetizer task 只包装一个未使用的 last 位；板上调试时
    // 该中间层会增加流控不确定性，所以这里保留 128 深度 FIFO 后直接消费。
    // 这里也不再把 y 写回 HBM；CuperPcg 内部直接拿 A*x0/A*p 更新 PCG 状态。
    tapa::stream<float_v16, 128>                            Pcg_Spmv_Stream("Pcg_Spmv_Stream");
    tapa::streams<INDEX_TYPE, 8, 2>                          Checker_Stop_Stream("Checker_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                              Sort_Stop_Stream("Sort_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                              Vector_Destroy_Stop_Stream("Vector_Destroy_Stop_Stream");
    tapa::stream<PcgStageEvent, 16>                          Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 16>                             Stage_Ticks_Stream("Stage_Ticks_Stream");

    tapa::task()
        // Controller 完成后广播 stop；所有 Pcg_* 服务任务收到 stop 后
        // 有限退出，避免 host 侧等待 AP_CTRL_HS completion 时卡住。
        .invoke(Pcg_Controller,
                Command_Stream,
                Matrix_Command_Stream,
                Checker_Stop_Stream,
                Sort_Stop_Stream,
                Vector_Destroy_Stop_Stream,
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Pcg_X_Stream,
                Pcg_Spmv_Stream,
                B,
                M_inv,
                X,
                R,
                Z,
                P,
                AP,
                Metrics,
                Status,
                Row_num,
                Max_iters,
                Tau)
        .invoke(Pcg_Stage_Timer, Stage_Event_Stream, Stage_Ticks_Stream)
        // Cuper SpMV 的参数/向量/矩阵输入端。Command_Stream[0] 给 ptr loader，
        // Command_Stream[1] 给 vector loader；Matrix_Command_Stream 分发到
        // 16 个矩阵 HBM channel。
        .invoke(Pcg_SpElement_list_ptr_Loader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Command_Stream[0],
                PE_Param[0])
        .invoke(Pcg_Vector_Loader,
                Column_num,
                Command_Stream[1],
                Pcg_X_Stream,
                Vector_X_Stream[0])
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Pcg_Matrix_Loader, Matrix_len, Matrix_data, Matrix_Command_Stream, Matrix_A_Stream)
        // 16 级 Cuper Core 链。PE_Param 和 Vector_X_Stream 在各级之间传递，
        // 每级消费一个 Matrix_data[channel]，输出该 channel 对 y 的部分贡献。
        //
        // 对第 i 级 core，可以按下面的通用形式读：
        //
        //   Pcg_Core(
        //       PE_Param[i],                  // 参数输入，来自上一级或 ptr loader
        //       Matrix_A_Stream[i],           // 第 i 个 HBM channel 的矩阵流
        //       Vector_X_Stream[i],           // 向量输入，来自上一级或 vector loader
        //       PE_Param[i + 1],              // 参数转发给下一级
        //       Vector_X_Stream[i + 1],       // 向量转发给下一级
        //       Vector_Y_Param[i],            // 给 accumulator 的输出行数/边界参数
        //       Matrix_Mult_Vector_Stream[i]) // 第 i 路矩阵分片产生的局部乘积
        //
        // [0..15] 表示 16 路 HBM 矩阵通道和 16 个 SpMV core；[16] 只表示
        // 串接链尾，不再对应新的矩阵通道。
        .invoke(Pcg_Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        .invoke(Pcg_Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        .invoke(Pcg_Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        .invoke(Pcg_Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        .invoke(Pcg_Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        .invoke(Pcg_Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        .invoke(Pcg_Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        .invoke(Pcg_Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        .invoke(Pcg_Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        .invoke(Pcg_Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        .invoke(Pcg_Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        .invoke(Pcg_Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        .invoke(Pcg_Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        .invoke(Pcg_Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        .invoke(Pcg_Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        .invoke(Pcg_Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 链尾 PE_Param[16] / Vector_X_Stream[16] 已经没有第 16 个 core 消费。
        // Destroy_* 常驻读取尾流，防止最后一级 core 写满 FIFO 后反压整条链。
        .invoke(Pcg_Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        .invoke(Pcg_Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM], Vector_Destroy_Stop_Stream)
        // Cuper 输出端：累加各 PE 部分和，过滤 padding，排序/拼包后直接
        // 回到 PCG controller，而不是像 Cuper(...) 那样写回 Y_out HBM。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Pcg_Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        .invoke<tapa::join, 8>(Pcg_Vector_Checker, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck, Checker_Stop_Stream)
        .invoke(Pcg_Mult_Sort_Tree, Vector_Y_Stream_Aftck, Pcg_Spmv_Stream, Sort_Stop_Stream)
    ;
}
