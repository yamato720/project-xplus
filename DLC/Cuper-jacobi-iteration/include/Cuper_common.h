#ifndef CUPER_COMMON
#define CUPER_COMMON

#include <vector>
#include <iostream>
#include <bitset>
#include <unordered_set>
#include <algorithm>

#include "Cuper.h"
#include "mmio_highlevel.h"

using namespace std;

// Host 侧使用的 COO 矩阵容器。
// 这里的 COO 是把所有非零元单独拿出来，三个数组一一对应：
//   Val[k] 位于原矩阵的 (RowIdx[k], ColIdx[k])。
// 当 Matrix_COO 被放进 SparseSlice::sliceVal 时，它表示一个非空 slice 块，
// 但 RowIdx/ColIdx 仍保存原矩阵的全局行列号，后续重排时才转换成局部列号
// 和 Cuper 内部 row 编码。
struct Matrix_COO {
    INDEX_TYPE         m;    // 矩阵或 slice 块的行数
    INDEX_TYPE         n;    // 矩阵或 slice 块的列数
    INDEX_TYPE         nnzR; // 非零元数量

    vector<INDEX_TYPE> ColIdx; // 非零元列号，和 Val 按下标一一对应
    vector<INDEX_TYPE> RowIdx; // 非零元行号，和 Val 按下标一一对应
    vector<VALUE_TYPE> Val;    // 非零元数值

    Matrix_COO() : m(0), n(0), nnzR(0), ColIdx() , RowIdx(), Val() {}
};

// 按 sliceSize x sliceSize 把稀疏矩阵切成块后的 host 侧结构。
// 它本质上是“按列 slice 压缩”的块稀疏格式：
//   sliceColPtr[colSlice]..sliceColPtr[colSlice + 1] 给出这一列 slice 下
//   所有非空块在 sliceRowIdx/sliceVal 中的范围。
//   sliceRowIdx[t] 是该非空块的行 slice 编号。
//   sliceVal[t] 是该块内所有非零元的 COO 列表。
// 也就是说，sliceVal 不是只放一个元素，而是把对应块里的所有非零元素塞进去。
struct SparseSlice {
    INDEX_TYPE         sliceSize;    // 每个块的边长
    INDEX_TYPE         numColSlices; // 列方向块数
    INDEX_TYPE         numRowSlices; // 行方向块数
    INDEX_TYPE         numSlices;    // 非空块数；创建过程中曾临时表示总块数

    vector<INDEX_TYPE> sliceColPtr; // 类 CSC 的列块指针，长度 numColSlices + 1
    vector<INDEX_TYPE> sliceRowIdx; // 每个非空块所在的行 slice 编号
    vector<Matrix_COO> sliceVal;    // 每个非空块内的所有非零元

    SparseSlice() : sliceSize(0), numColSlices(0), numRowSlices(0), sliceColPtr(), sliceRowIdx(), sliceVal() {}
};

// SpElement 是 Cuper SpMV 的非零元格式。
// host 侧会把 CSR/COO 非零元按 slice、PE、HBM channel 重排成
// SpElement list；TAPA kernel 只消费这些矩阵元素和输入向量 X。
// PCG 的 r/z/p/m_inv 等状态不在这个格式里。
struct SpElement{
    // colIdx 在 Reordering 前是全局列号；Reordering 后变成当前 column batch
    // 内的局部列号，最后打包进 64-bit slot 的 [63:50]。
    INDEX_TYPE colIdx;
    // rowIdx 在 Reordering 前是全局行号；Reordering 后变成 Cuper 内部 row
    // 编码，最后打包进 64-bit slot 的 [49:32]。-1 表示 host 侧 padding。
    INDEX_TYPE rowIdx;
    // FP32 非零元值，最后打包进 64-bit slot 的低 32 bit。
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

// 输入：
//   m/n/nnzR       : 原矩阵行数、列数、非零元数。
//   sliceSize      : 二维块边长；当前 Cuper 默认是 Slice_SIZE=64。
//   RowIdx_COO/... : 整张矩阵的 COO 非零元列表，长度均为 nnzR。
// 输出：
//   sliceMatrix    : 整张矩阵的 SparseSlice 总容器。函数会填充
//                    sliceSize/numRowSlices/numColSlices/numSlices，
//                    以及 sliceColPtr/sliceRowIdx/sliceVal。
// 注意：
//   sliceMatrix 是输出引用；调用前可以是空对象，函数内部会覆盖成新结果。
void Create_SparseSlice(const INDEX_TYPE m,
                        const INDEX_TYPE n,
                        const INDEX_TYPE nnzR,

                        const INDEX_TYPE sliceSize,

                        const vector<INDEX_TYPE> &RowIdx_COO,
                        const vector<INDEX_TYPE> &ColIdx_COO,
                        const vector<VALUE_TYPE> &Val_COO,

                        SparseSlice &sliceMatrix
                        ) {

    // 第一步：按 sliceSize 对全局矩阵做二维切块。每个非零元根据
    // (row / sliceSize, col / sliceSize) 归属到一个 slice 块。
    // 这里保留的是全局行列号，方便后面的 PE 分配继续按原始 row 做映射。
    // numColSlices/numRowSlices 是切块网格尺寸：
    //   numColSlices = ceil(n / sliceSize)
    //   numRowSlices = ceil(m / sliceSize)
    // 如果 m 或 n 不是 sliceSize 的整数倍，最后一行/列 slice 只覆盖矩阵边界内
    // 的真实元素；host buffer 对齐和 padding 在后续打包阶段处理。
    INDEX_TYPE numColSlices = (n + sliceSize - 1) / sliceSize;
    INDEX_TYPE numRowSlices = (m + sliceSize - 1) / sliceSize;

    INDEX_TYPE newnumCols  = numColSlices * sliceSize;
    INDEX_TYPE newnumRows  = numRowSlices * sliceSize;
    INDEX_TYPE newnnzR     = nnzR;

    if(newnumCols != n || newnumRows != m) {
        newnnzR += (newnumCols - n);
    }

    SparseSlice sliceMatrix_temp;

    // SparseSlice 的列块数量和行块数量描述切块网格；sliceVal 只保存非空块。
    sliceMatrix_temp.numColSlices = numColSlices;
    sliceMatrix_temp.numRowSlices = numRowSlices;
    sliceMatrix_temp.sliceSize    = sliceSize;

    INDEX_TYPE numSlices         = numColSlices * numRowSlices;

    sliceMatrix_temp.numSlices    = numSlices;

    vector<INDEX_TYPE> sliceCounts(numSlices, 0);
    for (INDEX_TYPE i = 0; i < nnzR; ++i) {
        INDEX_TYPE row       = RowIdx_COO[i];
        INDEX_TYPE col       = ColIdx_COO[i];
        // 对一个全局非零元 A(row, col)，整数除法直接定位它所在的块：
        //   sliceRow = row / sliceSize
        //   sliceCol = col / sliceSize
        // sliceIndex 采用列 slice 优先布局，后面才能构造成类似 CSC 的
        // sliceColPtr/sliceRowIdx/sliceVal。
        INDEX_TYPE sliceRow   = row / sliceSize;
        INDEX_TYPE sliceCol   = col / sliceSize;
        INDEX_TYPE sliceIndex = sliceCol * numRowSlices + sliceRow;
        sliceCounts[sliceIndex]++;
    }

    // 统计非空块数量。后面 sliceColPtr/sliceRowIdx/sliceVal 只为非空块分配空间。
    INDEX_TYPE numSlices_nnzR = 0;
    for(INDEX_TYPE i = 0; i < numSlices; ++i) {
        if(sliceCounts[i] != 0) numSlices_nnzR++;
    }

    sliceMatrix_temp.sliceColPtr.resize(numColSlices + 1, 0);
    sliceMatrix_temp.sliceRowIdx.resize(numSlices_nnzR, 0);

    // 构造按列 slice 压缩的块索引。对每个非空块，sliceVal 里先放一个
    // Matrix_COO 壳，后面再把该块内全部非零元 push 进去。
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

    // 前缀和后，sliceColPtr[j]..sliceColPtr[j + 1] 就是第 j 个列 slice
    // 下所有非空块在 sliceRowIdx/sliceVal 中的范围。
    for(INDEX_TYPE j = 0; j < numColSlices; ++j) {
        sliceMatrix_temp.sliceColPtr[j + 1] += sliceMatrix_temp.sliceColPtr[j];
    }

    // 建立“全网格 sliceIndex -> 非空块压缩数组下标”的映射。
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

        // 同一个 sliceOffset 对应一个非空块；这里把该块内的全部非零元追加到
        // 同一个 Matrix_COO 里。行列号仍是原矩阵全局坐标。
        sliceMatrix_temp.sliceRowIdx[sliceOffset] = sliceRow;
        sliceMatrix_temp.sliceVal[sliceOffset].ColIdx.push_back(col);
        sliceMatrix_temp.sliceVal[sliceOffset].RowIdx.push_back(row);
        sliceMatrix_temp.sliceVal[sliceOffset].Val.push_back(value);
    }

    sliceMatrix_temp.numSlices  = sliceMatrix_temp.sliceColPtr[numColSlices];
    sliceMatrix                = sliceMatrix_temp;
}

// 输入：
//   filename : Matrix Market 文件路径。
// 输出：
//   m/n/nnzR/isSymmetric : 只读取矩阵元信息，不读取 CSR 数据。
void Read_matrix_size(char       *filename,
                      INDEX_TYPE *m,
                      INDEX_TYPE *n,
                      INDEX_TYPE *nnzR,
                      INDEX_TYPE *isSymmetric
                     ) {

    mmio_info(m, n, nnzR, isSymmetric, filename);
}

// 输入：
//   filename : Matrix Market 文件路径。
//   m/n/nnzR : Read_matrix_size 读到的矩阵尺寸和非零元数量。
// 输出：
//   RowPtr/ColIdx/Val : CSR 三数组。调用前应已按 m+1 / nnzR 分配空间，
//                       函数把文件中的矩阵数据复制进去。
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

    // CPU reference：按 CSR 直接计算 Y_CPU = Y + A * X。
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

    // CPU reference：按 SparseSlice 遍历非空块，验证切块后仍等价于 Y + A * X。
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

// 输入：
//   m/n/nnzR   : 原矩阵尺寸和非零元数量；n 只表达矩阵形状，本函数内部不直接使用。
//   RowPtr     : CSR 行指针，长度 m + 1。
//   ColIdx/Val : CSR 非零元列号和值，长度 nnzR。
// 输出：
//   RowIdx_COO/ColIdx_COO/Val_COO：
//     COO 三数组，调用前应已按 nnzR 分配空间。函数会把每个非零元的
//     全局行号、全局列号和值写到相同下标。
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

    // CSR 的 RowPtr 隐式表达行号；COO 显式把每个非零元的全局行号写出来。
    // 结果满足 Val_COO[j] == A(RowIdx_COO[j], ColIdx_COO[j])。
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

    // 由 COO 生成 CSC：按列统计、前缀和，再把每个非零元放到对应列区间。
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

    // 相对误差检查；threshold 同时作为分母保护和误差阈值。
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

    // 预留的 slice 内行重排入口。当前实现立即 return，等价于保持原始行序。
    vector<INDEX_TYPE> RowIdx_copy(sliceVal.nnzR);

    for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        RowIdx_copy[i] = sliceVal.RowIdx[i];
    }
return;
  // for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        //if(((sliceVal.RowIdx[i] % 16) == 1) ||
           //((sliceVal.RowIdx[i] % 16) == 3) ||
           //((sliceVal.RowIdx[i] % 16) == 5) ||
           //((sliceVal.RowIdx[i] % 16) == 7)
           //) {
          //  RowIdx_copy[i] = sliceVal.RowIdx[i] + 8;
        //}
        //if(((sliceVal.RowIdx[i] % 16) == 9)  ||
          // ((sliceVal.RowIdx[i] % 16) == 11) ||
           //((sliceVal.RowIdx[i] % 16) == 13) ||
           //((sliceVal.RowIdx[i] % 16) == 15)
           //) {
         //   RowIdx_copy[i] = sliceVal.RowIdx[i] - 8;
       // }
   // }

    for(INDEX_TYPE i = 0; i < sliceVal.nnzR; ++i) {
        sliceVal.RowIdx[i] = RowIdx_copy[i];
    }
}

bool compare(SpElement sp1, SpElement sp2) {
	return sp1.colIdx < sp2.colIdx;
}

// 对单个 PE 的 SpElement 做局部调度。
// 输入 temp_SpElement_list 使用全局列号/全局行号；输出 SpEelment_list 使用：
//   colIdx = 全局列号 - 当前 column batch 的起始列号
//   rowIdx = Cuper 内部 row 编码
// 同一个 org_row_idx 的元素通过 sliding_window 拉开距离，降低后端累加器冲突。
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
        // Cuper accumulator 不是按原始全局 row 直接寻址，而是按
        // 2 * NUM_PE 的交错组来聚合。这个 org_row_idx 后面会被编码进
        // SpElement 的 rowIdx，硬件端再用它作为局部 URAM 累加地址。
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
        // 这里写入的是 Cuper 内部 row 编码，不是原始全局 row：
        //   bit0      : 原始 row 的奇偶，用来选择 ping/pong 累加阵列
        //   bit[17:1] : org_row_idx，作为局部累加地址
        //   bit17=1   : 空元素/padding 标记
        // 因此 65535 不是这个字段的直接全局行号边界。
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

// 输入：
//   NUM_PE      : 物理 PE 总数；当前通常是 HBM_CHANNEL_NUM * PE_NUM = 16 * 8 = 128。
//   NUM_ROW     : 原矩阵行数，Reordering 用它建立局部调度窗口。
//   NUM_COLUMN  : 原矩阵列数；当前保留为接口参数，本函数内部不直接使用。
//   Slice_SIZE  : SparseSlice 的块边长，用于计算 column batch 的起始全局列号。
//   BATCH_SIZE  : 每个 column batch 覆盖多少个列 slice。
//   sliceMatrix : Create_SparseSlice 生成的整矩阵分块 COO 总容器。
//   WINDOWS     : 同一内部 row 编码的元素在调度表中拉开的最小间隔。
// 输出：
//   SpElement_list_pes：
//     长度为 NUM_PE 的数组；SpElement_list_pes[p] 是第 p 个 PE 的元素列表。
//     函数会把每个 batch 内所有 PE 列表补齐到相同长度，空槽用 rowIdx=-1。
//   SpElement_list_ptr：
//     batch 边界表，长度为 Batch_num + 1。
//     ptr[i]..ptr[i+1] 是第 i 个 column batch 在每个 PE list 中的读取区间。
// 结果用途：
//   SpElement_list_pes 后续会被 Create_SpElement_list_for_all_channels 打包成
//   Matrix_data[16]；SpElement_list_ptr 会作为单独 HBM 输入传给 kernel。
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

    // 把 SparseSlice 转成“按物理 PE 分桶”的 SpElement 列表。
    // Batch_num 在 host/top 层通常就是这里的
    //   (numColSlices + BATCH_SIZE - 1) / BATCH_SIZE
    // SpElement_list_ptr 的长度是 Batch_num + 1：
    //   ptr[i]..ptr[i + 1] 是第 i 个 column batch 在每个 PE 列表中的读取区间。
    INDEX_TYPE numColSlices = sliceMatrix.numColSlices;
    SpElement_list_pes.resize(NUM_PE);
    SpElement_list_ptr.resize((numColSlices + BATCH_SIZE - 1) / BATCH_SIZE + 1, 0);

    vector<vector<SpElement> > temp_SpElement_list_pes(NUM_PE);
    for(INDEX_TYPE i = 0; i < (numColSlices + BATCH_SIZE - 1) / BATCH_SIZE; ++i) {
        // 每个 batch 覆盖连续 BATCH_SIZE 个列 slice。batch 内所有非空块的
        // 非零元先按目标 PE 暂存，batch 末尾再统一做 Reordering。
        for(INDEX_TYPE p = 0; p < NUM_PE; p++) {
            temp_SpElement_list_pes[p].resize(0);
        }
        for(INDEX_TYPE slicecolidx =  BATCH_SIZE * i; slicecolidx < min(BATCH_SIZE * (i + 1), numColSlices); ++slicecolidx) {
            for (INDEX_TYPE j = sliceMatrix.sliceColPtr[slicecolidx]; j < sliceMatrix.sliceColPtr[slicecolidx + 1]; ++j) {
                INDEX_TYPE slicennzR = sliceMatrix.sliceVal[j].nnzR;
                Sort_Slice_Row(sliceMatrix.sliceVal[j]);

                for(INDEX_TYPE k = 0; k < slicennzR; ++k) {
                    INDEX_TYPE row = sliceMatrix.sliceVal[j].RowIdx[k];
                    // 每两个 float 组成一个 float_v2 包。这里的分配顺序必须
                    // 和 8 路 Jacobi_UpdatePairCompute 的消费顺序一致，否则会出现
                    // 行号错位，板上大矩阵时也更容易卡在流控上。
                    INDEX_TYPE packet_id = row / 2;

                    // 关键映射变换：先映射到 8 个 update pair，再映射到
                    // pair 内的 ping/pong 偏移，最后映射到每组 8 个 PE。
                    INDEX_TYPE checker_id = packet_id % 8;
                    INDEX_TYPE acc_offset = (packet_id / 8) % 2;
                    INDEX_TYPE pe_in_acc  = (packet_id / 16) % 8;

                    // 重新组合物理 PE 编号。NUM_PE 在当前配置中是
                    // HBM_CHANNEL_NUM * PE_NUM，也就是 16 * 8 个物理槽位。
                    INDEX_TYPE p = (checker_id * 2 + acc_offset) * 8 + pe_in_acc;
                    temp_SpElement_list_pes[p].push_back(SpElement(sliceMatrix.sliceVal[j].ColIdx[k], sliceMatrix.sliceVal[j].RowIdx[k], sliceMatrix.sliceVal[j].Val[k]));
                }
            }
        }

        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            INDEX_TYPE i_start = SpElement_list_pes[p].size();
            INDEX_TYPE base_col_index = i * BATCH_SIZE * Slice_SIZE;
            // Reordering 会把全局列号转成当前 batch 的局部列号，并把全局行号
            // 转成硬件 accumulator 使用的内部 row 编码。
            Reordering(temp_SpElement_list_pes[p], SpElement_list_pes[p], base_col_index, i_start, NUM_ROW, NUM_PE, WINDOWS);
        }

        // 同一个 batch 的 128 个 PE 列表必须补齐到相同长度，这样后面能按
        // i 同步打包成 16 路 HBM，每路 8 个 PE slot。
        INDEX_TYPE max_len = 0;
        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            max_len = max((INDEX_TYPE) SpElement_list_pes[p].size(), max_len);
        }

        for(INDEX_TYPE p = 0; p < NUM_PE; ++p) {
            SpElement_list_pes[p].resize(max_len, SpElement(-1, -1, 0.0));
        }
        // ptr[i + 1] 是处理完第 i 个 batch 后每个 PE 列表的累计长度。
        SpElement_list_ptr[i + 1] = max_len;
    }
} // <--- 确保这个函数结束的右括号存在

// 紧接着下面应该是 Create_SpElement_list_for_all_channels

// 输入：
//   SpElement_list_pes : Create_SpElement_list_for_all_PEs 的输出，每个 PE 一条元素列表。
//   SpElement_list_ptr : batch 边界表；最后一个元素就是每个 PE list 的有效长度 max_len。
//   HBM_CHANNEL_NUM    : HBM 通道数，当前默认 16。
// 输出：
//   Matrix_fpga_data   : HBM_CHANNEL_NUM 路 host buffer。
//     Matrix_fpga_data[c] 保存第 c 路 HBM 的矩阵 payload；每 8 个 unsigned long
//     后续 reinterpret 成一个 ap_uint<512> beat。
//     SpElement_list_pes 里的 SpElement 不再以结构体形式传给 kernel，
//     而是在这里被压成 64-bit slot 写入 Matrix_fpga_data[c]。
// 打包关系：
//   channel c 的第 i 个 512-bit beat 含 8 个 64-bit SpElement slot；
//   slot j 对应 PE = c * 8 + j。
void Create_SpElement_list_for_all_channels(const vector<vector<SpElement> > &SpElement_list_pes,
                                            const vector<INDEX_TYPE>         &SpElement_list_ptr,
                                            vector<vector<unsigned long, tapa::aligned_allocator<unsigned long> > > &Matrix_fpga_data,
                                            const int HBM_CHANNEL_NUM = 16
                                           ) {

    // 把 128 个 PE 列表合并为 16 个 HBM channel 的 packed 矩阵数组。
    // 这是 SpElement 数据从“host 结构体列表”变成“kernel HBM payload”的边界：
    //   SpElement_list_pes[pe_idx][i]
    //     -> 64-bit packed slot x
    //     -> Matrix_fpga_data[c][i * 8 + j]
    //
    // 后面 host 会把 Matrix_fpga_data reinterpret 成 ap_uint<512>，传入
    // Cuper(...) 的 Matrix_data[0..15] 端口。
    //
    // 具体位置：
    //   Matrix_fpga_data[c][i * 8 + j] 对应 channel c、第 i 个 512-bit beat、
    //   第 j 个 64-bit SpElement slot，也就是 PE c * 8 + j。
    // Matrix_len 在 top 层通常就是 max_len；每个 Matrix_data[channel] 读取
    // Matrix_len 个 512-bit beat。这里的 host buffer 为 512 个 unsigned long
    // 对齐，可能比 Matrix_len * 8 更长，但有效数据由 Matrix_len 控制。
    INDEX_TYPE max_len = SpElement_list_ptr[SpElement_list_ptr.size() - 1];
    INDEX_TYPE Matrix_fpga_data_column_size = 8 * max_len;
    INDEX_TYPE Matrix_fpga_data_channel_size = ((Matrix_fpga_data_column_size + 511) / 512) * 512;
    // XRT/TAPA cannot create a zero-byte BO. A diagonal-only R matrix legitimately
    // has Matrix_len=0, so keep the kernel length at zero but allocate one padded
    // 512-bit beat per HBM channel for the host mmap.
    if (Matrix_fpga_data_channel_size == 0) {
        Matrix_fpga_data_channel_size = 8;
    }

    for(INDEX_TYPE c = 0; c < HBM_CHANNEL_NUM; ++c) {
        Matrix_fpga_data[c].assign(Matrix_fpga_data_channel_size, 0);
    }

    // 每个 HBM channel 的一个 512-bit beat 包含 8 个 64-bit SpElement：
    //   [63:50] colIdx，相对当前 column batch 的局部列号，14 bit
    //   [49:32] rowIdx，Reordering 后的 Cuper 内部 row 编码，18 bit
    //   [31:0]  value，float32 非零值
    // rowIdx=0x3ffff 表示空槽；有效元素 bit17 必须为 0。
    for(INDEX_TYPE i = 0; i < max_len; ++i) {
        for(INDEX_TYPE c = 0; c < HBM_CHANNEL_NUM; ++c) {
            for(INDEX_TYPE j = 0; j < 8; ++j) {
                // 这里的 pe_idx 必须和 Create_SpElement_list_for_all_PEs 的
                // 交错映射完全一致：物理通道 c 的第 j 个 64-bit 槽位，
                // 对应 SpElement_list_pes 里的第 c * 8 + j 个 PE。
                INDEX_TYPE pe_idx = c * 8 + j;

                SpElement sp = SpElement_list_pes[pe_idx][i];

                unsigned long x = 0;
                if (sp.rowIdx == -1) {
                    x = 0x3FFFFULL << 32; // 空行标记
                } else {
                    unsigned int x_float_bits = *(unsigned int*)(&sp.val);
                    unsigned long x_val_64 = (unsigned long)(x_float_bits & 0xFFFFFFFFULL);

                    // 这里真正把 SpElement 结构体拆成硬件要读的 bit layout：
                    //   sp.val    -> x_val_64 -> bits [31:0]
                    //   sp.rowIdx -> x_row    -> bits [49:32]
                    //   sp.colIdx -> x_col    -> bits [63:50]
                    // 从这一行之后，kernel 看到的就不再是 C++ struct，
                    // 而是 Matrix_data 中的 64-bit packed slot。
                    // 注意这里的 rowIdx 变换：
                    // 在 Reordering 函数里，rowIdx 被改成了 org_row_idx * 2 + (original_row % 2)
                    // 我们直接打包转换后的 rowIdx
                    unsigned long x_row = ((unsigned long)sp.rowIdx & 0x3FFFFULL) << 32;
                    unsigned long x_col = ((unsigned long)sp.colIdx & 0x3FFFULL) << 50;

                    x = x_col | x_row | x_val_64;
                }
                // 存入第 c 通道的第 i 个 512-bit beat 的第 j 个 64-bit 槽位
                Matrix_fpga_data[c][j + i * 8] = x;
            }
        }
    }
}
#endif
