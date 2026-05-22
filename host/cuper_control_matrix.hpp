#ifndef PROJECT_XPLUS_CUPER_CONTROL_MATRIX_HPP
#define PROJECT_XPLUS_CUPER_CONTROL_MATRIX_HPP

#include "../include/cg_common.hpp"
#include "dataset_bridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace project_xplus::cgsolver {

// host 侧生成 cuper_pcg_control_kernel 直接消费的矩阵格式。
//
// 数据组织方式和 kernel 中的 Cuper 常量一一对应：
//   - 列方向按 8192 宽度切 batch
//   - 行方向按 Cuper 的 128 个物理 PE 分发
//   - 16 个 matrix_data[channel] 对应 kernel 的 16 个 m_axi HBM 端口
//   - 每个 channel 的连续 8 个 word 是同一拍要送入 8 条 lane 的数据
struct CuperControlMatrix {
    static constexpr int kSliceWidth = 8192;
    static constexpr int kHbmChannelNum = 16;
    static constexpr int kPePerHbm = 8;
    static constexpr int kPhysicalPeNum = kHbmChannelNum * kPePerHbm;
    // 同一个 row group 的非零元至少隔 kReorderWindow 个 slot，
    // 减少 kernel 侧同一 URAM 地址连续累加造成的数据相关冲突。
    static constexpr int kReorderWindow = 10;

    // batch b 的有效 index 范围是
    // [sp_element_list_ptr[b], sp_element_list_ptr[b + 1])。
    std::vector<index_t> sp_element_list_ptr;
    std::vector<std::vector<unsigned long>> matrix_data;
    int batch_count = 0;
    int matrix_len = 0;
};

// 打包前的逻辑非零元。col 保存全局列号，进入 pack 前会转成
// 当前 8192 列 batch 内的 local_col。
struct CuperPackedElement {
    index_t col = -1;
    index_t row = -1;
    float value = 0.0f;
};

// 把全局行号映射到 Cuper 物理 PE。
// kernel 侧 restore_global_row() 依赖这里的 checker/acc/lane 分配规则反解。
inline int cuper_physical_pe_for_row(const int row) {
    const int packet_id = row / 2;
    const int checker_id = packet_id % 8;
    const int acc_offset = (packet_id / 8) % 2;
    const int pe_in_acc = (packet_id / 16) % 8;
    return (checker_id * 2 + acc_offset) * 8 + pe_in_acc;
}

// 避免数值转换，直接保留 fp32 的 bit pattern。
inline unsigned int float_to_bits(const float value) {
    union {
        float f;
        unsigned int u;
    } converter{};
    converter.f = value;
    return converter.u;
}

// 64-bit packed word layout:
//   [63:50] local_col
//   [49:32] packed_row
//   [31:0]  fp32 value bits
//
// row/col 为负数表示 padding 元素。padding row 被写成 0x3ffff，
// kernel 侧检查 row bit17 后直接跳过。
inline unsigned long pack_cuper_element(const CuperPackedElement& element) {
    if (element.row < 0 || element.col < 0) {
        return 0x3FFFFUL << 32;
    }

    const unsigned long value_bits = static_cast<unsigned long>(float_to_bits(element.value));
    const unsigned long row_bits = (static_cast<unsigned long>(element.row) & 0x3FFFFUL) << 32;
    const unsigned long col_bits = (static_cast<unsigned long>(element.col) & 0x3FFFUL) << 50;
    return col_bits | row_bits | value_bits;
}

inline void append_reordered_cuper_elements(std::vector<CuperPackedElement>& input,
                                            std::vector<CuperPackedElement>& output,
                                            const int base_col,
                                            const int n) {
    // 同一 PE 内先按列排序，让读取 x_slice 更接近顺序访问。
    std::sort(input.begin(), input.end(), [](const CuperPackedElement& lhs,
                                             const CuperPackedElement& rhs) {
        return lhs.col < rhs.col;
    });

    const CuperPackedElement empty;
    std::vector<CuperPackedElement> scheduled;
    // sliding_window[row_group] 记录这个 row group 最近一次被排到哪个 slot。
    // 新元素至少从 old + kReorderWindow 开始找空位。
    std::vector<index_t> sliding_window(static_cast<std::size_t>(n), -CuperControlMatrix::kReorderWindow);

    for (const CuperPackedElement& element : input) {
        const int original_row_group = element.row / (2 * CuperControlMatrix::kPhysicalPeNum);
        int scheduled_index =
            sliding_window[static_cast<std::size_t>(original_row_group)] +
            CuperControlMatrix::kReorderWindow;

        while (true) {
            if (scheduled_index >= static_cast<int>(scheduled.size())) {
                scheduled.resize(static_cast<std::size_t>(scheduled_index + 1), empty);
            }
            const CuperPackedElement& current = scheduled[static_cast<std::size_t>(scheduled_index)];
            if (current.row < 0 && current.col < 0) {
                break;
            }
            ++scheduled_index;
        }

        // row 在 PE 内转成局部 row group 编码：
        //   高位是 group，低位表示原始全局行的偶/奇。
        scheduled[static_cast<std::size_t>(scheduled_index)] = CuperPackedElement{
            element.col - base_col,
            original_row_group * 2 + (element.row & 1),
            element.value,
        };
        sliding_window[static_cast<std::size_t>(original_row_group)] = scheduled_index;
    }

    output.insert(output.end(), scheduled.begin(), scheduled.end());
}

inline CuperControlMatrix build_cuper_control_matrix(const Dataset& dataset) {
    CuperControlMatrix matrix;
    matrix.batch_count = (dataset.n() + CuperControlMatrix::kSliceWidth - 1) /
                         CuperControlMatrix::kSliceWidth;
    matrix.sp_element_list_ptr.assign(static_cast<std::size_t>(matrix.batch_count + 1), 0);

    // pe_lists 是最终跨所有 batch 拼起来的 128 路 PE 流。
    // batch_pe_elements 先按 batch 和 PE 暂存，便于每个 batch 独立 reorder。
    std::vector<std::vector<CuperPackedElement>> pe_lists(
        static_cast<std::size_t>(CuperControlMatrix::kPhysicalPeNum));

    std::vector<std::vector<CuperPackedElement>> batch_pe_elements(
        static_cast<std::size_t>(matrix.batch_count * CuperControlMatrix::kPhysicalPeNum));
    for (int row = 0; row < dataset.n(); ++row) {
        for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
             offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
             ++offset) {
            const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
            const int batch = col / CuperControlMatrix::kSliceWidth;
            const int pe = cuper_physical_pe_for_row(row);
            // kernel 的 SpMV 用 fp32 矩阵值乘 fp64 x，PCG 向量和归约仍是 data_t。
            batch_pe_elements[static_cast<std::size_t>(
                batch * CuperControlMatrix::kPhysicalPeNum + pe)].push_back(CuperPackedElement{
                col,
                row,
                static_cast<float>(dataset.values()[static_cast<std::size_t>(offset)])
            });
        }
    }

    for (int batch = 0; batch < matrix.batch_count; ++batch) {
        const int base_col = batch * CuperControlMatrix::kSliceWidth;
        for (int pe = 0; pe < CuperControlMatrix::kPhysicalPeNum; ++pe) {
            append_reordered_cuper_elements(
                batch_pe_elements[static_cast<std::size_t>(
                    batch * CuperControlMatrix::kPhysicalPeNum + pe)],
                pe_lists[static_cast<std::size_t>(pe)],
                base_col,
                dataset.n());
        }

        int max_len = 0;
        for (const auto& pe_list : pe_lists) {
            max_len = std::max(max_len, static_cast<int>(pe_list.size()));
        }
        // 每个 batch 结束时把 128 条 PE 流补齐到同一长度。
        // sp_element_list_ptr 记录补齐后的累计长度，kernel 可用统一 begin/end。
        for (auto& pe_list : pe_lists) {
            pe_list.resize(static_cast<std::size_t>(max_len));
        }
        matrix.sp_element_list_ptr[static_cast<std::size_t>(batch + 1)] = max_len;
    }

    matrix.matrix_len = matrix.sp_element_list_ptr.back();
    const int matrix_word_count = 8 * matrix.matrix_len;
    // channel_size 按 512 word 对齐，便于 HBM BO 分配和后续 burst 访问。
    const int channel_size = ((matrix_word_count + 511) / 512) * 512;
    matrix.matrix_data.assign(
        static_cast<std::size_t>(CuperControlMatrix::kHbmChannelNum),
        std::vector<unsigned long>(static_cast<std::size_t>(channel_size), 0));

    // 把 128 路 PE 流重新排成 16 个 HBM channel。
    // channel 内的 layout 是 index-major，每个 index 连续 8 lane。
    for (int index = 0; index < matrix.matrix_len; ++index) {
        for (int channel = 0; channel < CuperControlMatrix::kHbmChannelNum; ++channel) {
            for (int lane = 0; lane < CuperControlMatrix::kPePerHbm; ++lane) {
                const int pe = channel * CuperControlMatrix::kPePerHbm + lane;
                const auto& element = pe_lists[static_cast<std::size_t>(pe)]
                                              [static_cast<std::size_t>(index)];
                matrix.matrix_data[static_cast<std::size_t>(channel)]
                                  [static_cast<std::size_t>(index * 8 + lane)] =
                    pack_cuper_element(element);
            }
        }
    }

    return matrix;
}

}  // namespace project_xplus::cgsolver

#endif
