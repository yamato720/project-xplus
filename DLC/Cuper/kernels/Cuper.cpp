#include <ap_int.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <tapa.h>

#include "Cuper.h"

using namespace std;

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
