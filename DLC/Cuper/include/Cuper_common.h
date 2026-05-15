#ifndef CUPER_COMMON
#define CYPER_COMMON

#include <vector>
#include <iostream>
#include <bitset>
#include <unordered_set>
#include <algorithm>

#include "Cuper.h"

#ifdef BINARY_READ
#include "biio.h"
#include "mmio_highlevel_b.h"
#else
#include "mmio_highlevel.h"
#endif

using namespace std;

struct Matrix_COO {
    INDEX_TYPE         m;
    INDEX_TYPE         n;
    INDEX_TYPE         nnzR;

    vector<INDEX_TYPE> ColIdx;
    vector<INDEX_TYPE> RowIdx;
    vector<VALUE_TYPE> Val;

    Matrix_COO() : m(0), n(0), nnzR(0), ColIdx() , RowIdx(), Val() {}
};

struct SparseSlice {
    INDEX_TYPE         sliceSize;
    INDEX_TYPE         numColSlices;
    INDEX_TYPE         numRowSlices;
    INDEX_TYPE         numSlices;

    vector<INDEX_TYPE> sliceColPtr;
    vector<INDEX_TYPE> sliceRowIdx;
    vector<Matrix_COO> sliceVal;

    SparseSlice() : sliceSize(0), numColSlices(0), numRowSlices(0), sliceColPtr(), sliceRowIdx(), sliceVal() {}
};

struct SpElement{
    INDEX_TYPE colIdx;
    INDEX_TYPE rowIdx;
    VALUE_TYPE val;
    
    SpElement(INDEX_TYPE colidx = -1, INDEX_TYPE rowidx = -1, VALUE_TYPE value = 0.0): colIdx(colidx), rowIdx(rowidx), val(value) {}
    
    SpElement& operator=(const SpElement& sp) {
        colIdx = sp.colIdx;
        rowIdx = sp.rowIdx;
        val    = sp.val;
        return *this;
    }
};

void Display_Matrix(const INDEX_TYPE m, 
                    const INDEX_TYPE n, 
                    const INDEX_TYPE nnzR,

                    const vector<INDEX_TYPE> &RowIdx_COO,
                    const vector<INDEX_TYPE> &ColIdx_COO,
                    const vector<VALUE_TYPE> &Val_COO
                   ) {
    
    INDEX_TYPE k = 0, zero = 0;
    for(INDEX_TYPE i = 0; i < m; ++i) {
        for(INDEX_TYPE j = 0; j < n; ++j) {
            if(i == RowIdx_COO[k] && j== ColIdx_COO[k]) {
                cout << std::setw(3) << std::setfill(' ') << Val_COO[k] << ' ';
                ++k;
            }
            else
                cout << std::setw(3) << std::setfill(' ') << zero << ' ';
        }
        cout << endl;
    }
}

void Display_SliceMatrix(const SparseSlice &sliceMatrix_temp) {

    INDEX_TYPE idxSlices = 0;
    for(INDEX_TYPE j = 0; j < sliceMatrix_temp.numColSlices; ++j) {
        for(INDEX_TYPE i = sliceMatrix_temp.sliceColPtr[j]; i < sliceMatrix_temp.sliceColPtr[j + 1]; ++i) {
            cout << "---------------------Slice[" << idxSlices << "]---------------------" << endl;
            INDEX_TYPE rowIdx = sliceMatrix_temp.sliceRowIdx[i];
            cout << "SliceRowIdx = " << rowIdx << " SliceColIdx = " << j;
            INDEX_TYPE slicennzR = sliceMatrix_temp.sliceVal[i].nnzR;
            cout << " ElementNums = " << slicennzR << endl;
            for(INDEX_TYPE k = 0; k < slicennzR; ++k) {
                cout << "Element[" << k << "] RowIdx = " << sliceMatrix_temp.sliceVal[i].RowIdx[k] << " ColIdx = " << sliceMatrix_temp.sliceVal[i].ColIdx[k] << " Val = " << sliceMatrix_temp.sliceVal[i].Val[k] << endl;
            }
            idxSlices++;
            cout << "-------------------------------------------------\n" << endl;
        }
    }
}

void Create_SparseSlice(const INDEX_TYPE m, 
                        const INDEX_TYPE n, 
                        const INDEX_TYPE nnzR,

                        const INDEX_TYPE sliceSize,

                        const vector<INDEX_TYPE> &RowIdx_COO,
                        const vector<INDEX_TYPE> &ColIdx_COO,
                        const vector<VALUE_TYPE> &Val_COO,

                        SparseSlice &sliceMatrix
                        ) {
    
    INDEX_TYPE numColSlices = (n + sliceSize - 1) / sliceSize;
    INDEX_TYPE numRowSlices = (m + sliceSize - 1) / sliceSize;

    INDEX_TYPE newnumCols  = numColSlices * sliceSize;
    INDEX_TYPE newnumRows  = numRowSlices * sliceSize;
    INDEX_TYPE newnnzR     = nnzR;

    if(newnumCols != n || newnumRows != m) {
        newnnzR += (newnumCols - n);
    }

    SparseSlice sliceMatrix_temp;

    sliceMatrix_temp.numColSlices = numColSlices;
    sliceMatrix_temp.numRowSlices = numRowSlices;
    sliceMatrix_temp.sliceSize    = sliceSize;

    INDEX_TYPE numSlices         = numColSlices * numRowSlices;

    sliceMatrix_temp.numSlices    = numSlices;

    vector<INDEX_TYPE> sliceCounts(numSlices, 0);
    for (INDEX_TYPE i = 0; i < nnzR; ++i) {
        INDEX_TYPE row       = RowIdx_COO[i];
        INDEX_TYPE col       = ColIdx_COO[i];
        INDEX_TYPE sliceRow   = row / sliceSize;
        INDEX_TYPE sliceCol   = col / sliceSize;
        INDEX_TYPE sliceIndex = sliceCol * numRowSlices + sliceRow;
        sliceCounts[sliceIndex]++;
    }

    INDEX_TYPE numSlices_nnzR = 0;
    for(INDEX_TYPE i = 0; i < numSlices; ++i) {
        if(sliceCounts[i] != 0) numSlices_nnzR++;
    }

    sliceMatrix_temp.sliceColPtr.resize(numColSlices + 1, 0);
    sliceMatrix_temp.sliceRowIdx.resize(numSlices_nnzR, 0);

    for(INDEX_TYPE j = 0; j < numColSlices; ++j) {
        for(INDEX_TYPE i = 0; i < numRowSlices; ++i) {
            INDEX_TYPE sliceIndex = j * numRowSlices + i;
            if(sliceCounts[sliceIndex] != 0) {
                sliceMatrix_temp.sliceColPtr[j + 1] += 1;
                Matrix_COO cooElem_temp;
                cooElem_temp.m    = sliceSize;
                cooElem_temp.n    = sliceSize;
                cooElem_temp.nnzR = sliceCounts[sliceIndex];
                sliceMatrix_temp.sliceVal.push_back(cooElem_temp);
            } 
        }
    }

    for(INDEX_TYPE j = 0; j < numColSlices; ++j) {
        sliceMatrix_temp.sliceColPtr[j + 1] += sliceMatrix_temp.sliceColPtr[j];
    }
    
    vector<INDEX_TYPE> sliceOffsets(numSlices, 0);
    INDEX_TYPE offset = 0;
    for(INDEX_TYPE i = 0; i < numSlices; ++i) {
        if(sliceCounts[i] != 0) {
            sliceOffsets[i] = offset;
            offset++;
        }
    }

    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        INDEX_TYPE row        = RowIdx_COO[i];
        INDEX_TYPE col        = ColIdx_COO[i];
        VALUE_TYPE value      = Val_COO[i];
        INDEX_TYPE sliceRow    = row / sliceSize;
        INDEX_TYPE sliceCol    = col / sliceSize;
        INDEX_TYPE sliceIndex  = sliceCol * numRowSlices + sliceRow;
        INDEX_TYPE sliceOffset = sliceOffsets[sliceIndex];

        sliceMatrix_temp.sliceRowIdx[sliceOffset] = sliceRow;
        sliceMatrix_temp.sliceVal[sliceOffset].ColIdx.push_back(col);
        sliceMatrix_temp.sliceVal[sliceOffset].RowIdx.push_back(row);
        sliceMatrix_temp.sliceVal[sliceOffset].Val.push_back(value);
    }

    sliceMatrix_temp.numSlices  = sliceMatrix_temp.sliceColPtr[numColSlices];
    sliceMatrix                = sliceMatrix_temp;
}

#ifdef BINARY_READ

void Read_binary_matrix_2_CSR(char       *filename, 
                              INDEX_TYPE &m, 
                              INDEX_TYPE &n, 
                              INDEX_TYPE &nnzR,
                              INDEX_TYPE &isSymmetric,

                              vector<INDEX_TYPE> &RowPtr, 
                              vector<INDEX_TYPE> &ColIdx, 
                              vector<VALUE_TYPE> &Val
                             ) {
                     
    INDEX_TYPE *RowPtr_tmp;
    INDEX_TYPE *ColIdx_tmp;
    double     *Val_fp64;

    read_Dmatrix_32(&m, &n, &nnzR, &RowPtr_tmp, &ColIdx_tmp, &Val_fp64, &isSymmetric, filename);

    RowPtr.resize(m + 1);
    ColIdx.resize(nnzR);
    Val.resize(nnzR);

    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        Val[i] = Val_fp64[i];
        ColIdx[i] = ColIdx_tmp[i];
    }

    for(INDEX_TYPE i = 0; i < m + 1; ++i) {
        RowPtr[i] = RowPtr_tmp[i];
    }

    free(Val_fp64);
    free(ColIdx_tmp);
    free(RowPtr_tmp);
}
#else

void Read_matrix_size(char       *filename,
                      INDEX_TYPE *m, 
                      INDEX_TYPE *n, 
                      INDEX_TYPE *nnzR,
                      INDEX_TYPE *isSymmetric
                     ) {

    mmio_info(m, n, nnzR, isSymmetric, filename);
}

void Read_matrix_2_CSR(char       *filename, 
                       INDEX_TYPE m, 
                       INDEX_TYPE n, 
                       INDEX_TYPE nnzR,

                       vector<INDEX_TYPE> &RowPtr, 
                       vector<INDEX_TYPE> &ColIdx, 
                       vector<VALUE_TYPE> &Val
                      ) {

    INDEX_TYPE *RowPtr_d = (INDEX_TYPE *)malloc(sizeof(INDEX_TYPE) * (m + 1));
    INDEX_TYPE *ColIdx_d = (INDEX_TYPE *)malloc(sizeof(INDEX_TYPE) * nnzR);
    VALUE_TYPE *Val_d    = (VALUE_TYPE *)malloc(sizeof(VALUE_TYPE) * nnzR);

    mmio_data(RowPtr_d, ColIdx_d, Val_d, filename);

    for(INDEX_TYPE i = 0; i < m + 1; ++i)
        RowPtr[i] = RowPtr_d[i];
    
    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        ColIdx[i] = ColIdx_d[i];
        Val[i]    = Val_d[i];
    }

    free(Val_d);
    free(ColIdx_d);
    free(RowPtr_d);
}
#endif

void SpMV_CPU_CSR(const INDEX_TYPE m, 
                  const INDEX_TYPE n, 
                  const INDEX_TYPE nnzR,

                  const vector<INDEX_TYPE> &RowPtr, 
                  const vector<INDEX_TYPE> &ColIdx, 
                  const vector<VALUE_TYPE> &Val,

                  const vector<VALUE_TYPE> &X, 
                  const vector<VALUE_TYPE> &Y,
                  vector<VALUE_TYPE>       &Y_CPU
                 ) {
    
    for(INDEX_TYPE i = 0; i < m; ++i) {
        VALUE_TYPE sum = Y[i];
        for(INDEX_TYPE j = RowPtr[i]; j < RowPtr[i + 1]; ++j) {
            sum += X[ColIdx[j]] * Val[j];
        }
        Y_CPU[i] = sum;
    }
}

void SpMV_CPU_Slice(const INDEX_TYPE m, 
                    const INDEX_TYPE n, 
                    const INDEX_TYPE nnzR,

                    const SparseSlice &sliceMatrix,

                    const vector<VALUE_TYPE> &X, 
                    const vector<VALUE_TYPE> &Y,
                    vector<VALUE_TYPE>       &Y_CPU
                   ) {

    for(INDEX_TYPE i = 0; i < m; ++i)
        Y_CPU[i] = Y[i];
    
    for(INDEX_TYPE j = 0; j < sliceMatrix.numColSlices; ++j) {
        for(INDEX_TYPE i = sliceMatrix.sliceColPtr[j]; i < sliceMatrix.sliceColPtr[j + 1]; ++i) {
            INDEX_TYPE slicennzR = sliceMatrix.sliceVal[i].nnzR;

            for(INDEX_TYPE k = 0; k < slicennzR; ++k) {
                INDEX_TYPE colIdx = sliceMatrix.sliceVal[i].ColIdx[k];
                INDEX_TYPE rowIdx = sliceMatrix.sliceVal[i].RowIdx[k];
                Y_CPU[rowIdx]     += X[colIdx] * sliceMatrix.sliceVal[i].Val[k];
                cout << "i = " << rowIdx << " j = " << colIdx << " val = " << sliceMatrix.sliceVal[i].Val[k] << " x = " << X[colIdx] << endl;
            }
        }
    }
}

void CSR_2_COO(const INDEX_TYPE m, 
               const INDEX_TYPE n, 
               const INDEX_TYPE nnzR,

               const vector<INDEX_TYPE> &RowPtr, 
               const vector<INDEX_TYPE> &ColIdx, 
               const vector<VALUE_TYPE> &Val,

               vector<INDEX_TYPE> &RowIdx_COO,
               vector<INDEX_TYPE> &ColIdx_COO,
               vector<VALUE_TYPE> &Val_COO
             ) {

    INDEX_TYPE row = 0;
    for(INDEX_TYPE i = 0; i < m; ++i) {
        for(INDEX_TYPE j = RowPtr[i]; j < RowPtr[i + 1]; ++j) {
            RowIdx_COO[j] = row;
            ColIdx_COO[j] = ColIdx[j];
            Val_COO[j]    = Val[j];
        }
        row++;
    }
}

void COO_2_CSC(const INDEX_TYPE m, 
               const INDEX_TYPE n, 
               const INDEX_TYPE nnzR,
               const vector<INDEX_TYPE> &RowIdx_COO,
               const vector<INDEX_TYPE> &ColIdx_COO,
               const vector<VALUE_TYPE> &Val_COO, 
               vector<INDEX_TYPE> &ColPtr, 
               vector<INDEX_TYPE> &RowIdx, 
               vector<VALUE_TYPE> &Val
              ) {

    ColPtr.resize(n + 1, 0);

    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        INDEX_TYPE col = ColIdx_COO[i];
        ColPtr[col + 1]++;
    }

    for(INDEX_TYPE i = 1; i <= n; ++i) {
        ColPtr[i] += ColPtr[i - 1];
    }

    RowIdx.resize(nnzR);
    Val.resize(nnzR);

    for(INDEX_TYPE i = 0; i < nnzR; ++i) {
        INDEX_TYPE row   = RowIdx_COO[i];
        INDEX_TYPE col   = ColIdx_COO[i];
        VALUE_TYPE val   = Val_COO[i];
        INDEX_TYPE index = ColPtr[col];
        RowIdx[index]    = row;
        Val[index]       = val;
        ColPtr[col]++;
    }

    for(INDEX_TYPE i = n; i > 0; --i) {
        ColPtr[i] = ColPtr[i - 1];
    }

    ColPtr[0] = 0;
}

INDEX_TYPE Verify_correctness(const INDEX_TYPE n,
                              const vector<VALUE_TYPE> &Y_org,
                              const vector<VALUE_TYPE> &Y,
                              const double             threshold = 1e-4
                             ) {

    INDEX_TYPE error_num = 0;
    for(INDEX_TYPE i = 0; i < n; ++i) {
        VALUE_TYPE y_org = Y_org[i];
        VALUE_TYPE y     = Y[i];
        VALUE_TYPE dff   = fabs(y_org - y);
        VALUE_TYPE x     = min(fabs(y_org), fabs(y)) + threshold;
        error_num        += (dff / x > threshold); 
    }
    return error_num;
}

void Sort_Slice_Row(Matrix_COO &sliceVal) {

    vector<INDEX_TYPE> RowIdx_copy(sliceVal.nnzR);

    for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        RowIdx_copy[i] = sliceVal.RowIdx[i];
    }

    for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        if(((sliceVal.RowIdx[i] % 16) == 1) ||
           ((sliceVal.RowIdx[i] % 16) == 3) ||
           ((sliceVal.RowIdx[i] % 16) == 5) ||
           ((sliceVal.RowIdx[i] % 16) == 7)
           ) {
            RowIdx_copy[i] = sliceVal.RowIdx[i] + 8;
        }
        if(((sliceVal.RowIdx[i] % 16) == 9)  ||
           ((sliceVal.RowIdx[i] % 16) == 11) ||
           ((sliceVal.RowIdx[i] % 16) == 13) ||
           ((sliceVal.RowIdx[i] % 16) == 15)
           ) {
            RowIdx_copy[i] = sliceVal.RowIdx[i] - 8;
        }
    }
    
    for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        sliceVal.RowIdx[i] = RowIdx_copy[i];
    }
}

bool compare(SpElement sp1, SpElement sp2) {
	return sp1.colIdx < sp2.colIdx;
}

void Reordering(vector<SpElement> &temp_SpElement_list,
                vector<SpElement> &SpEelment_list,
                const INDEX_TYPE base_col_index,
                const INDEX_TYPE i_start,
                const INDEX_TYPE NUM_Row,
                const INDEX_TYPE NUM_PE,
                const INDEX_TYPE WIDTH
                ) {

    sort(temp_SpElement_list.begin(), temp_SpElement_list.end(), compare);

    SpElement sp_empty = {-1, -1, 0.0};

    vector<SpElement> scheduled_SpElement;
    
    vector<INDEX_TYPE> sliding_window(NUM_Row, -WIDTH);
    INDEX_TYPE org_row_idx;

    for(INDEX_TYPE p = 0; p < temp_SpElement_list.size(); ++p) {
        org_row_idx = temp_SpElement_list[p].rowIdx / (2 * NUM_PE);
        INDEX_TYPE win_row_idx = sliding_window[org_row_idx] + WIDTH;
        INDEX_TYPE insert_flag = 1;
        while(insert_flag){
            if(win_row_idx >= ((INDEX_TYPE)scheduled_SpElement.size())) {
                scheduled_SpElement.resize(win_row_idx + 1);
                scheduled_SpElement[win_row_idx] = sp_empty;
            }
            SpElement sp = scheduled_SpElement[win_row_idx];
            if(sp.rowIdx == -1 && sp.colIdx == -1 && sp.val == 0.0) {
                insert_flag = 0;
            }
            else {
                win_row_idx++;
            }
        }

        scheduled_SpElement[win_row_idx].colIdx = temp_SpElement_list[p].colIdx - base_col_index;
        scheduled_SpElement[win_row_idx].rowIdx = org_row_idx * 2 + (temp_SpElement_list[p].rowIdx % 2);
        scheduled_SpElement[win_row_idx].val = temp_SpElement_list[p].val;
        sliding_window[org_row_idx] = win_row_idx;
    }

    INDEX_TYPE scheduled_SpElement_size = scheduled_SpElement.size();

    if (scheduled_SpElement_size > 0) {
        SpEelment_list.resize(i_start + scheduled_SpElement_size, sp_empty);
        for(INDEX_TYPE i = 0; i < scheduled_SpElement_size; ++i) {
            SpEelment_list[i + i_start] = scheduled_SpElement[i];
        }
    }
}

void Create_SpElement_list_for_all_PEs(const INDEX_TYPE NUM_PE,
                                       const INDEX_TYPE NUM_ROW,
                                       const INDEX_TYPE NUM_COLUMN,
                                       const INDEX_TYPE Slice_SIZE,
                                       const INDEX_TYPE BATCH_SIZE,

                                       SparseSlice &sliceMatrix,
                                       vector<vector<SpElement> > &SpElement_list_pes,
                                       vector<INDEX_TYPE> &SpElement_list_ptr,
                                       const INDEX_TYPE WINDOWS = 10
                                      ) {

    INDEX_TYPE numColSlices = sliceMatrix.numColSlices;
    SpElement_list_pes.resize(NUM_PE);
    SpElement_list_ptr.resize((numColSlices + BATCH_SIZE - 1) / BATCH_SIZE + 1, 0);

    vector<vector<SpElement> > temp_SpElement_list_pes(NUM_PE);
    for(INDEX_TYPE i = 0; i < (numColSlices + BATCH_SIZE - 1) / BATCH_SIZE; ++i) {
        for(INDEX_TYPE p = 0; p < NUM_PE; p++) {
            temp_SpElement_list_pes[p].resize(0);
        }
        for(INDEX_TYPE slicecolidx =  BATCH_SIZE * i; slicecolidx < min(BATCH_SIZE * (i + 1), numColSlices); ++slicecolidx) {
            for (INDEX_TYPE j = sliceMatrix.sliceColPtr[slicecolidx]; j < sliceMatrix.sliceColPtr[slicecolidx + 1]; ++j) {
                INDEX_TYPE slicennzR = sliceMatrix.sliceVal[j].nnzR;
                
                Sort_Slice_Row(sliceMatrix.sliceVal[j]);

                for(INDEX_TYPE k = 0; k < slicennzR; ++k) {
                    INDEX_TYPE p = (sliceMatrix.sliceVal[j].RowIdx[k] / 2) % NUM_PE;
                    INDEX_TYPE pos = temp_SpElement_list_pes[p].size();
                    temp_SpElement_list_pes[p].resize(pos + 1);
                    temp_SpElement_list_pes[p][pos] = SpElement(sliceMatrix.sliceVal[j].ColIdx[k], sliceMatrix.sliceVal[j].RowIdx[k], sliceMatrix.sliceVal[j].Val[k]); //将稀疏元素非配给PE
                }
            }
        } 

        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            INDEX_TYPE i_start = SpElement_list_pes[p].size();
            INDEX_TYPE base_col_index = i * BATCH_SIZE * Slice_SIZE;
            Reordering(temp_SpElement_list_pes[p],
                       SpElement_list_pes[p],
                       base_col_index,
                       i_start,
                       NUM_ROW,
                       NUM_PE,
                       WINDOWS
                      );

        }
        INDEX_TYPE max_len = 0;
        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            max_len = max((INDEX_TYPE) SpElement_list_pes[p].size(), max_len);
        }
        
        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            SpElement_list_pes[p].resize(max_len, SpElement(-1, -1, 0.0));
        }
        
        SpElement_list_ptr[i + 1] = max_len;
    } 
}

void Create_SpElement_list_for_all_channels(const vector<vector<SpElement> > &SpElement_list_pes,
                                            const vector<INDEX_TYPE>         &SpElement_list_ptr,
                                            vector<vector<unsigned long, tapa::aligned_allocator<unsigned long> > > &Matrix_fpga_data,
                                            const int HBM_CHANNEL_NUM = 8
                                           ) {
        
    INDEX_TYPE Matrix_fpga_data_column_size = 8 * SpElement_list_ptr[SpElement_list_ptr.size() - 1] * 4 / 4;
    INDEX_TYPE Matrix_fpga_data_channel_size  = ((Matrix_fpga_data_column_size + 512 - 1) / 512) * 512;

    for(INDEX_TYPE c = 0; c < HBM_CHANNEL_NUM; ++c) {
        Matrix_fpga_data[c].resize(Matrix_fpga_data_channel_size, 0);
    }
    
    for(INDEX_TYPE i = 0; i < SpElement_list_ptr[SpElement_list_ptr.size() - 1]; ++i) {
        for(INDEX_TYPE c = 0; c < HBM_CHANNEL_NUM; ++c) {
            for(INDEX_TYPE j = 0; j < 8; ++j) {
                SpElement sp = SpElement_list_pes[j + c * 8][i];

                unsigned long x = 0;
                if (sp.rowIdx == -1) {
                    x = 0x3FFFF;
                    x = x << 32;
                } else {
                    unsigned long x_col = sp.colIdx;
                    x_col = (x_col & 0x3FFF) << (32 + 18);
                    
                    unsigned long x_row = sp.rowIdx;
                    x_row = (x_row & 0x3FFFF) << 32;
                    VALUE_TYPE x_float = sp.val;
                    
                    unsigned int x_float_in_int = *((unsigned int*)(&x_float));
                    unsigned long x_float_val_64 = ((unsigned long) x_float_in_int);
                    x_float_val_64 = x_float_val_64 & 0xFFFFFFFF;

                    x = x_col | x_row | x_float_val_64;

                }
                if(HBM_CHANNEL_NUM != 8 && HBM_CHANNEL_NUM != 16 && HBM_CHANNEL_NUM != 24) {
                    cout << "Please check HBM_CHANNEL_NUM!" << endl;
                    exit(1);
                }
                else if(HBM_CHANNEL_NUM == 8) {
                    INDEX_TYPE pe_idx = j + c * 8;
                    
                    INDEX_TYPE pix_m8 = pe_idx % 8;
                    Matrix_fpga_data[(pix_m8 % 8) * 1 + pix_m8 / 8][(pe_idx % 64) / 8 + i * 8] = x;
                }
                else if(HBM_CHANNEL_NUM == 16) {
                    INDEX_TYPE pe_idx = j + c * 8;
                    INDEX_TYPE pix_m16 = pe_idx % 16;
                    Matrix_fpga_data[(pix_m16 % 8) * 2 + pix_m16 / 8][(pe_idx % 128) / 16 + i * 8] = x;
                }
                else if(HBM_CHANNEL_NUM == 24) {
                    INDEX_TYPE pe_idx = j + c * 8;
                    INDEX_TYPE pix_m24 = pe_idx % 24;
                    Matrix_fpga_data[(pix_m24 % 8) * 3 + pix_m24 / 8][(pe_idx % 192) / 24 + i * 8] = x;
                }
            }
        }
    }
}

#endif