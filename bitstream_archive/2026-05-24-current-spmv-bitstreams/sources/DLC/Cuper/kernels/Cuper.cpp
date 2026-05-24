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
};

struct PcgSpmvPacket {
    // SpMV 输出从 float_v16 包装成 packet 送回 PCG controller。
    // last 当前只作为边界标记保留，controller 仍按 Row_num 推导包数消费。
    float_v16 values;
    bool last;
};

static constexpr INDEX_TYPE kPcgStatusConverged = 0;
static constexpr INDEX_TYPE kPcgStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgStatusBreakdown = 2;
static constexpr double kPcgBreakdownEps = 1.0e-30;

inline double pcg_abs(const double value) {
#pragma HLS inline
    return value < 0.0 ? -value : value;
}

inline bool pcg_invalid(const double value) {
#pragma HLS inline
    return value != value;
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
                        tapa::ostream<float_v2> &Vector_Y_Stream_Aftck) {
    const INDEX_TYPE num_pe_output =
        ((Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2) *
        HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE num_out = (Row_num + 15) >> 4;

    for (;;) {
#pragma HLS loop_flatten off
    out:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
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

// 将 Cuper SpMV 的 float_v16 结果转成 PCG controller 消费的 packet 流。
//
// 原始 Cuper 顶层这里会写回 Y_out HBM；CuperPcg 不写临时 y 到 HBM，
// 而是直接把 A*x0 或 A*p 流回 controller，用来初始化 r/z/p 或计算 p^T A p。
void Pcg_Vector_Packetizer(const INDEX_TYPE Row_num,
                           tapa::istream<float_v16> &Vector_Y_Stream_Ans,
                           tapa::ostream<PcgSpmvPacket> &Spmv_out) {
    const INDEX_TYPE packet_count = (Row_num + 15) >> 4;

    for (;;) {
#pragma HLS loop_flatten off
    write_packets:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            if (!Vector_Y_Stream_Ans.empty() && !Spmv_out.full()) {
                PcgSpmvPacket out;
                Vector_Y_Stream_Ans.try_read(out.values);
                out.last = (packet + 1 == packet_count);
                Spmv_out.try_write(out);
            }
        }
    }
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
                    tapa::ostream<float_v16> &X_to_spmv,
                    tapa::istream<PcgSpmvPacket> &Spmv_in,
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
    const INDEX_TYPE packet_count = (Row_num + 15) >> 4;
    INDEX_TYPE status_code = kPcgStatusMaxIter;
    INDEX_TYPE iterations = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    CuperSpmvCommand command;
    command.iteration_num = 1;

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
// 初始化 SpMV：先用当前 X 计算 A*x0。
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

send_x0:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
            float_v16 x_packet;
    fill_x0_packet:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                const INDEX_TYPE index = (packet << 4) + lane;
                double value = 0.0;
                if (index < Row_num) {
                    value = X[index];
                }
                x_packet[lane] = static_cast<VALUE_TYPE>(value);
            }
            X_to_spmv.write(x_packet);
        }

init_vectors:
        // 消费 A*x0，完成 Jacobi-PCG 初始 r/z/p，同时累计 rz 和 rr。
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
            const PcgSpmvPacket ap_packet = Spmv_in.read();
    init_lanes:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                const INDEX_TYPE index = (packet << 4) + lane;
                if (index < Row_num) {
                    const double b_value = B[index];
                    const double minv_value = M_inv[index];
                    const double ap_value = static_cast<double>(ap_packet.values[lane]);
                    const double r_value = b_value - ap_value;
                    const double z_value = minv_value * r_value;
                    R[index] = r_value;
                    Z[index] = z_value;
                    P[index] = z_value;
                    rz += r_value * z_value;
                    rr += r_value * r_value;
                }
            }
        }

pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
    // 每轮 SpMV：将当前搜索方向 p 送入 Cuper 流水，计算 AP=A*p。
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

    send_p:
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                float_v16 p_packet;
        fill_p_packet:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                    const INDEX_TYPE index = (packet << 4) + lane;
                    double value = 0.0;
                    if (index < Row_num) {
                        value = P[index];
                    }
                    p_packet[lane] = static_cast<VALUE_TYPE>(value);
                }
                X_to_spmv.write(p_packet);
            }

            p_ap = 0.0;
    consume_ap:
            // 消费 AP，同时累计 p^T AP。AP 写回 HBM 便于调试和 host 检查。
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const PcgSpmvPacket ap_packet = Spmv_in.read();
        ap_lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                    const INDEX_TYPE index = (packet << 4) + lane;
                    if (index < Row_num) {
                        const double p_value = P[index];
                        const double ap_value = static_cast<double>(ap_packet.values[lane]);
                        AP[index] = ap_value;
                        p_ap += p_value * ap_value;
                    }
                }
            }

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

            double rz_new = 0.0;
            double rr_new = 0.0;
    update_xrz:
            // x/r/z 更新仍在 controller 内顺序完成。这里的双精度加乘
            // 和多 HBM 读写会集中到一个较大的流水里。II=1 在 U55C 上
            // 容易把 controller 周边布线挤爆，因此放宽到 II=2，优先
            // 保证 full-FPGA 版本能稳定 route。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=2
                const double x_value = X[index];
                const double p_value = P[index];
                const double r_value = R[index];
                const double ap_value = AP[index];
                const double minv_value = M_inv[index];
                const double x_new = x_value + alpha * p_value;
                const double r_new = r_value - alpha * ap_value;
                const double z_new = minv_value * r_new;
                X[index] = x_new;
                R[index] = r_new;
                Z[index] = z_new;
                rz_new += r_new * z_new;
                rr_new += r_new * r_new;
            }

            if (pcg_invalid(rz_new) || pcg_invalid(rr_new)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            const double beta = rz_new / rz;
            if (pcg_invalid(beta)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

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

            rz = rz_new;
            rr = rr_new;
            iterations = iter + 1;
        }

        if (status_code != kPcgStatusBreakdown) {
            status_code = (rr <= Tau) ? kPcgStatusConverged : kPcgStatusMaxIter;
        }
    }

    // Metrics/Status 是 host 侧判断运行结果和调试数值稳定性的最小输出。
    Metrics[0] = rz;
    Metrics[1] = rr;
    Metrics[2] = p_ap;
    Metrics[3] = alpha;
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
    // 这样 host 只 launch 一次 CuperPcg；PCG 每轮迭代都在这个 TAPA
    // task graph 内部完成，不再走 host 侧循环调用 Cuper。
    tapa::streams<CuperSpmvCommand, 2, 4>                   Command_Stream("Command_Stream");
    tapa::streams<CuperSpmvCommand, HBM_CHANNEL_NUM, 4>     Matrix_Command_Stream("Matrix_Command_Stream");
    tapa::stream<float_v16, 128>                            Pcg_X_Stream("Pcg_X_Stream");
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>     PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>      Vector_X_Stream("Vector_X_Stream");
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 64>        Matrix_A_Stream("Matrix_A_Stream");
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>          Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>      Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>           Vector_Y_Stream("Vector_Y_Stream");
    tapa::streams<float_v2, 8, FIFO_DEPTH>                  Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    tapa::stream<float_v16, FIFO_DEPTH>                     Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");
    tapa::stream<PcgSpmvPacket, 128>                        Pcg_Spmv_Stream("Pcg_Spmv_Stream");

    tapa::task()
        // 唯一 join 的任务：PCG controller 完成后整个 kernel 才返回。
        // 其他 Pcg_* task 都是常驻服务，等待 controller 发下一次 SpMV 命令。
        .invoke(Pcg_Controller,
                Command_Stream,
                Matrix_Command_Stream,
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
        // Cuper SpMV 的参数/向量/矩阵输入端。Command_Stream[0] 给 ptr loader，
        // Command_Stream[1] 给 vector loader；Matrix_Command_Stream 分发到
        // 16 个矩阵 HBM channel。
        .invoke<tapa::detach>(Pcg_SpElement_list_ptr_Loader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Command_Stream[0],
                PE_Param[0])
        .invoke<tapa::detach>(Pcg_Vector_Loader,
                Column_num,
                Command_Stream[1],
                Pcg_X_Stream,
                Vector_X_Stream[0])
        .invoke<tapa::detach, HBM_CHANNEL_NUM>(Pcg_Matrix_Loader, Matrix_len, Matrix_data, Matrix_Command_Stream, Matrix_A_Stream)
        // 16 级 Cuper Core 链。PE_Param 和 Vector_X_Stream 在各级之间传递，
        // 每级消费一个 Matrix_data[channel]，输出该 channel 对 y 的部分贡献。
        .invoke<tapa::detach>(Pcg_Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        .invoke<tapa::detach>(Pcg_Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        .invoke<tapa::detach>(Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        .invoke<tapa::detach>(Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM])
        // Cuper 输出端：累加各 PE 部分和，过滤 padding，排序/拼包后直接
        // 回到 PCG controller，而不是像 Cuper(...) 那样写回 Y_out HBM。
        .invoke<tapa::detach, HBM_CHANNEL_NUM>(Pcg_Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        .invoke<tapa::detach, 8>(Pcg_Vector_Checker, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck)
        .invoke<tapa::detach>(Mult_Sort_Tree, Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans)
        .invoke<tapa::detach>(Pcg_Vector_Packetizer, Row_num, Vector_Y_Stream_Ans, Pcg_Spmv_Stream)
    ;
}
