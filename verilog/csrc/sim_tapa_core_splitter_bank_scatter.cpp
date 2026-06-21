#include "VCuperSpmvOnly_CoreStrip.h"
#include "VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.h"
#include "VCuperSpmvOnly_SourceLaneSplitterOoo.h"
#include "VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.h"

#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>

namespace {

constexpr int kSources = 8;
constexpr int kHbmChannels = 16;
constexpr int kLanes = 8;
constexpr int kPacketsPerSource = 2;
constexpr int kPairs = 8;
constexpr int kScalars = 16;
constexpr int kRowNum = 16;
constexpr int kColumnNum = 16;
constexpr int kBatchNum = 1;
constexpr int kIterationNum = 1;
constexpr int kTimeoutCycles = 20000;

using CoreTop = VCuperSpmvOnly_CoreStrip;
using SplitterTop = VCuperSpmvOnly_SourceLaneSplitterOoo;
using BankTop = VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo;
using ScatterTop =
    VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter;

using Word33 = uint64_t;
using Word129 = std::array<uint32_t, 5>;
using Word130 = std::array<uint32_t, 5>;
using Word401 = std::array<uint32_t, 13>;
using Word513 = std::array<uint32_t, 17>;

uint32_t fbits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float fval(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

template <size_t N>
void set_bits(std::array<uint32_t, N>& word, int lo, int width,
              uint64_t value) {
  for (int bit = 0; bit < width; ++bit) {
    if ((value >> bit) & 1ULL) {
      const int pos = lo + bit;
      word[static_cast<size_t>(pos / 32)] |=
          1U << static_cast<unsigned>(pos % 32);
    }
  }
}

template <size_t N>
void drive_word(VlWide<N>& port, const std::array<uint32_t, N>& word) {
  for (size_t i = 0; i < N; ++i) {
    port[i] = word[i];
  }
}

template <size_t N>
std::array<uint32_t, N> read_word(const VlWide<N>& port) {
  std::array<uint32_t, N> word{};
  for (size_t i = 0; i < N; ++i) {
    word[i] = port[i];
  }
  return word;
}

Word513 pack_x_packet() {
  Word513 word{};
  for (int lane = 0; lane < 16; ++lane) {
    set_bits(word, lane * 32, 32, fbits(1.0f));
  }
  return word;
}

Word513 pack_matrix_packet(uint32_t row0, uint32_t val0) {
  Word513 word{};
  for (int lane = 0; lane < kLanes; ++lane) {
    const int base = lane * 64;
    const uint32_t row = (lane == 0) ? row0 : (1U << 17);
    set_bits(word, base + 0, 32, lane == 0 ? val0 : 0);
    set_bits(word, base + 32, 18, row);
    set_bits(word, base + 50, 14, 0);
  }
  return word;
}

uint32_t expected_value(int scalar_idx) {
  if ((scalar_idx & 1) == 0) {
    return fbits(10.0f + static_cast<float>(scalar_idx >> 1));
  }
  return fbits(100.0f + static_cast<float>(scalar_idx >> 1));
}

void eval_core(CoreTop& core, VerilatedContext& context, int clk) {
  core.ap_clk = clk;
  core.eval();
  context.timeInc(1);
}

void eval_splitter(SplitterTop& splitter, VerilatedContext& context, int clk) {
  splitter.ap_clk = clk;
  splitter.eval();
  context.timeInc(1);
}

void eval_bank(BankTop& bank, VerilatedContext& context, int clk) {
  bank.ap_clk = clk;
  bank.eval();
  context.timeInc(1);
}

void eval_scatter(ScatterTop& scatter, VerilatedContext& context, int clk) {
  scatter.ap_clk = clk;
  scatter.eval();
  context.timeInc(1);
}

void clear_core_unused_inputs(CoreTop& core) {
  Word513 zero513{};
  Word401 zero401{};
  core.PE_Param_in_peek_dout = 0;
  core.PE_Param_in_peek_empty_n = 0;
  drive_word(core.Matrix_A_Stream_peek_dout, zero513);
  core.Matrix_A_Stream_peek_empty_n = 0;
  drive_word(core.Vector_X_Stream_in_peek_dout, zero513);
  core.Vector_X_Stream_in_peek_empty_n = 0;
  core.PE_Param_out_peek = 0;
  drive_word(core.Vector_X_Stream_out_peek, zero513);
  core.Vector_Y_Param_peek = 0;
  drive_word(core.Matrix_Mult_Vector_Stream_peek, zero401);
}

void drive_core_inputs(CoreTop& core,
                       const std::deque<Word33>& param_fifo,
                       const std::deque<Word513>& matrix_fifo,
                       const std::deque<Word513>& x_fifo) {
  Word513 zero513{};
  if (!param_fifo.empty()) {
    core.PE_Param_in_s_dout = param_fifo.front();
    core.PE_Param_in_s_empty_n = 1;
  } else {
    core.PE_Param_in_s_dout = 0;
    core.PE_Param_in_s_empty_n = 0;
  }
  if (!matrix_fifo.empty()) {
    drive_word(core.Matrix_A_Stream_s_dout, matrix_fifo.front());
    core.Matrix_A_Stream_s_empty_n = 1;
  } else {
    drive_word(core.Matrix_A_Stream_s_dout, zero513);
    core.Matrix_A_Stream_s_empty_n = 0;
  }
  if (!x_fifo.empty()) {
    drive_word(core.Vector_X_Stream_in_s_dout, x_fifo.front());
    core.Vector_X_Stream_in_s_empty_n = 1;
  } else {
    drive_word(core.Vector_X_Stream_in_s_dout, zero513);
    core.Vector_X_Stream_in_s_empty_n = 0;
  }
}

void clear_splitter_unused_inputs(SplitterTop& splitter) {
  Word33 zero33{};
  Word401 zero401{};
  Word130 zero130{};
  splitter.Vector_Y_Param_peek_dout = zero33;
  splitter.Vector_Y_Param_peek_empty_n = 0;
  drive_word(splitter.Matrix_Mult_Vector_Stream_peek_dout, zero401);
  splitter.Matrix_Mult_Vector_Stream_peek_empty_n = 0;
  drive_word(splitter.Owner_Lane_Stream_0_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_1_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_2_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_3_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_4_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_5_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_6_peek, zero130);
  drive_word(splitter.Owner_Lane_Stream_7_peek, zero130);
}

void drive_splitter_inputs(SplitterTop& splitter,
                           const std::deque<Word33>& param_fifo,
                           const std::deque<Word401>& matmult_fifo) {
  Word401 zero401{};
  if (!param_fifo.empty()) {
    splitter.Vector_Y_Param_s_dout = param_fifo.front();
    splitter.Vector_Y_Param_s_empty_n = 1;
  } else {
    splitter.Vector_Y_Param_s_dout = 0;
    splitter.Vector_Y_Param_s_empty_n = 0;
  }
  if (!matmult_fifo.empty()) {
    drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout,
               matmult_fifo.front());
    splitter.Matrix_Mult_Vector_Stream_s_empty_n = 1;
  } else {
    drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout, zero401);
    splitter.Matrix_Mult_Vector_Stream_s_empty_n = 0;
  }
}

bool splitter_lane0_write(const SplitterTop& splitter) {
  return splitter.Owner_Lane_Stream_0_s_write;
}

Word130 splitter_lane0_word(const SplitterTop& splitter) {
  return read_word(splitter.Owner_Lane_Stream_0_s_din);
}

void clear_bank_peek_ports(BankTop& bank) {
  Word130 zero_in{};
  Word129 zero_out{};
  drive_word(bank.Owner_Lane_Stream_0_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_1_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_2_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_3_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_4_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_5_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_6_peek_dout, zero_in);
  drive_word(bank.Owner_Lane_Stream_7_peek_dout, zero_in);
  bank.Owner_Lane_Stream_0_peek_empty_n = 0;
  bank.Owner_Lane_Stream_1_peek_empty_n = 0;
  bank.Owner_Lane_Stream_2_peek_empty_n = 0;
  bank.Owner_Lane_Stream_3_peek_empty_n = 0;
  bank.Owner_Lane_Stream_4_peek_empty_n = 0;
  bank.Owner_Lane_Stream_5_peek_empty_n = 0;
  bank.Owner_Lane_Stream_6_peek_empty_n = 0;
  bank.Owner_Lane_Stream_7_peek_empty_n = 0;
  drive_word(bank.Vector_Y_Tagged_Stream_peek, zero_out);
}

void drive_bank_lane(BankTop& bank, int lane,
                     const std::deque<Word130>& fifo) {
  const bool valid = !fifo.empty();
  Word130 zero{};
  const Word130& word = valid ? fifo.front() : zero;
  switch (lane) {
    case 0:
      drive_word(bank.Owner_Lane_Stream_0_s_dout, word);
      bank.Owner_Lane_Stream_0_s_empty_n = valid ? 1 : 0;
      break;
    case 1:
      drive_word(bank.Owner_Lane_Stream_1_s_dout, word);
      bank.Owner_Lane_Stream_1_s_empty_n = valid ? 1 : 0;
      break;
    case 2:
      drive_word(bank.Owner_Lane_Stream_2_s_dout, word);
      bank.Owner_Lane_Stream_2_s_empty_n = valid ? 1 : 0;
      break;
    case 3:
      drive_word(bank.Owner_Lane_Stream_3_s_dout, word);
      bank.Owner_Lane_Stream_3_s_empty_n = valid ? 1 : 0;
      break;
    case 4:
      drive_word(bank.Owner_Lane_Stream_4_s_dout, word);
      bank.Owner_Lane_Stream_4_s_empty_n = valid ? 1 : 0;
      break;
    case 5:
      drive_word(bank.Owner_Lane_Stream_5_s_dout, word);
      bank.Owner_Lane_Stream_5_s_empty_n = valid ? 1 : 0;
      break;
    case 6:
      drive_word(bank.Owner_Lane_Stream_6_s_dout, word);
      bank.Owner_Lane_Stream_6_s_empty_n = valid ? 1 : 0;
      break;
    default:
      drive_word(bank.Owner_Lane_Stream_7_s_dout, word);
      bank.Owner_Lane_Stream_7_s_empty_n = valid ? 1 : 0;
      break;
  }
}

bool bank_lane_read(const BankTop& bank, int lane) {
  switch (lane) {
    case 0: return bank.Owner_Lane_Stream_0_s_read;
    case 1: return bank.Owner_Lane_Stream_1_s_read;
    case 2: return bank.Owner_Lane_Stream_2_s_read;
    case 3: return bank.Owner_Lane_Stream_3_s_read;
    case 4: return bank.Owner_Lane_Stream_4_s_read;
    case 5: return bank.Owner_Lane_Stream_5_s_read;
    case 6: return bank.Owner_Lane_Stream_6_s_read;
    default: return bank.Owner_Lane_Stream_7_s_read;
  }
}

void clear_scatter_unused(ScatterTop& scatter) {
  scatter.Vector_Y_Tagged_Stream_1_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_2_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_3_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_4_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_5_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_6_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_7_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_8_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_9_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_10_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_11_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_12_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_13_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_14_empty_n = 0;
  scatter.Vector_Y_Tagged_Stream_15_empty_n = 0;
}

}  // namespace

int main(int argc, char** argv) {
  VerilatedContext context;
  context.commandArgs(argc, argv);

  std::array<std::unique_ptr<CoreTop>, kSources> cores;
  std::array<std::unique_ptr<SplitterTop>, kSources> splitters;
  for (int i = 0; i < kSources; ++i) {
    cores[static_cast<size_t>(i)] = std::make_unique<CoreTop>();
    splitters[static_cast<size_t>(i)] = std::make_unique<SplitterTop>();
  }
  BankTop bank;
  ScatterTop scatter;

  std::array<std::deque<Word33>, kSources> core_param_fifo;
  std::array<std::deque<Word513>, kSources> core_matrix_fifo;
  std::array<std::deque<Word513>, kSources> core_x_fifo;
  std::array<std::deque<Word33>, kSources> splitter_param_fifo;
  std::array<std::deque<Word401>, kSources> splitter_matmult_fifo;
  std::array<std::deque<Word130>, kSources> lane_fifo;
  std::deque<Word129> bank_to_scatter_fifo;

  for (int source = 0; source < kSources; ++source) {
    const int core_id = source * 2;
    auto& params = core_param_fifo[static_cast<size_t>(source)];
    params.push_back(kBatchNum);
    params.push_back(kRowNum);
    params.push_back(kIterationNum);
    params.push_back(kColumnNum);
    for (int i = core_id; i < kHbmChannels; ++i) {
      params.push_back(0);
    }
    for (int i = core_id; i < kHbmChannels; ++i) {
      params.push_back(kPacketsPerSource);
    }

    core_x_fifo[static_cast<size_t>(source)].push_back(pack_x_packet());
    core_matrix_fifo[static_cast<size_t>(source)].push_back(
        pack_matrix_packet(0, fbits(10.0f + static_cast<float>(source))));
    core_matrix_fifo[static_cast<size_t>(source)].push_back(
        pack_matrix_packet(1, fbits(100.0f + static_cast<float>(source))));
  }

  std::array<uint32_t, kScalars> y{};
  std::array<bool, kScalars> seen{};

  int core_param_reads = 0;
  int core_matrix_reads = 0;
  int core_x_reads = 0;
  int core_y_param_writes = 0;
  int core_matmult_writes = 0;
  int splitter_param_reads = 0;
  int splitter_matmult_reads = 0;
  int splitter_writes = 0;
  int bank_reads = 0;
  int bank_pairs = 0;
  int scatter_reads = 0;
  int writes = 0;
  int pending_responses = 0;
  std::array<bool, kSources> core_done_seen{};
  std::array<bool, kSources> splitter_done_seen{};
  bool bank_done_seen = false;
  bool scatter_done_seen = false;

  for (uint64_t cycle = 0; cycle < kTimeoutCycles; ++cycle) {
    const bool rst = cycle < 5;
    const bool start_pulse = cycle == 5;
    const bool response_available = !rst && pending_responses > 0;

    for (int source = 0; source < kSources; ++source) {
      auto& core = *cores[static_cast<size_t>(source)];
      const int core_id = source * 2;
      core.ap_rst_n = rst ? 0 : 1;
      core.ap_start = (!rst && !core_done_seen[static_cast<size_t>(source)])
                          ? 1
                          : 0;
      core.Core_id = static_cast<uint32_t>(core_id);
      clear_core_unused_inputs(core);
      drive_core_inputs(core,
                        core_param_fifo[static_cast<size_t>(source)],
                        core_matrix_fifo[static_cast<size_t>(source)],
                        core_x_fifo[static_cast<size_t>(source)]);
      core.PE_Param_out_s_full_n = 1;
      core.Vector_X_Stream_out_s_full_n = 1;
      core.Vector_Y_Param_s_full_n =
          splitter_param_fifo[static_cast<size_t>(source)].size() < 64 ? 1 : 0;
      core.Matrix_Mult_Vector_Stream_s_full_n =
          splitter_matmult_fifo[static_cast<size_t>(source)].size() < 64 ? 1 : 0;

      auto& splitter = *splitters[static_cast<size_t>(source)];
      splitter.ap_rst_n = rst ? 0 : 1;
      splitter.ap_start =
          (!rst && !splitter_done_seen[static_cast<size_t>(source)]) ? 1 : 0;
      splitter.Source_id = static_cast<uint32_t>(core_id);
      clear_splitter_unused_inputs(splitter);
      drive_splitter_inputs(splitter,
                            splitter_param_fifo[static_cast<size_t>(source)],
                            splitter_matmult_fifo[static_cast<size_t>(source)]);
      splitter.Owner_Lane_Stream_0_s_full_n =
          lane_fifo[static_cast<size_t>(source)].size() < 64 ? 1 : 0;
      splitter.Owner_Lane_Stream_1_s_full_n = 1;
      splitter.Owner_Lane_Stream_2_s_full_n = 1;
      splitter.Owner_Lane_Stream_3_s_full_n = 1;
      splitter.Owner_Lane_Stream_4_s_full_n = 1;
      splitter.Owner_Lane_Stream_5_s_full_n = 1;
      splitter.Owner_Lane_Stream_6_s_full_n = 1;
      splitter.Owner_Lane_Stream_7_s_full_n = 1;
    }

    bank.ap_rst_n = rst ? 0 : 1;
    bank.ap_start = (!rst && !bank_done_seen) ? 1 : 0;
    bank.Iteration_num = kIterationNum;
    bank.Row_num = kRowNum;
    bank.Owner_id = 0;
    bank.Vector_Y_Tagged_Stream_s_full_n =
        bank_to_scatter_fifo.size() < 64 ? 1 : 0;
    clear_bank_peek_ports(bank);
    for (int lane = 0; lane < kSources; ++lane) {
      drive_bank_lane(bank, lane, lane_fifo[static_cast<size_t>(lane)]);
    }

    scatter.ap_rst = rst ? 1 : 0;
    scatter.ap_start = (!rst && !scatter_done_seen) ? 1 : 0;
    scatter.scalar_writes_total = kScalars;
    scatter.tagged_pairs_total = kPairs;
    scatter.Y_out_write_addr_s_full_n = 1;
    scatter.Y_out_write_data_s_full_n = 1;
    scatter.Y_out_write_addr_offset_load = 0;
    scatter.Y_out_write_resp_s_dout = 0;
    scatter.Y_out_write_resp_s_empty_n = response_available ? 1 : 0;
    clear_scatter_unused(scatter);
    if (!rst && !bank_to_scatter_fifo.empty()) {
      drive_word(scatter.Vector_Y_Tagged_Stream_0_dout,
                 bank_to_scatter_fifo.front());
      scatter.Vector_Y_Tagged_Stream_0_empty_n = 1;
    } else {
      Word129 zero{};
      drive_word(scatter.Vector_Y_Tagged_Stream_0_dout, zero);
      scatter.Vector_Y_Tagged_Stream_0_empty_n = 0;
    }

    for (auto& core : cores) eval_core(*core, context, 0);
    for (auto& splitter : splitters) eval_splitter(*splitter, context, 0);
    eval_bank(bank, context, 0);
    eval_scatter(scatter, context, 0);

    std::array<bool, kSources> core_param_read_pre{};
    std::array<bool, kSources> core_matrix_read_pre{};
    std::array<bool, kSources> core_x_read_pre{};
    std::array<bool, kSources> core_y_param_write_pre{};
    std::array<Word33, kSources> core_y_param_word_pre{};
    std::array<bool, kSources> core_matmult_write_pre{};
    std::array<Word401, kSources> core_matmult_word_pre{};
    std::array<bool, kSources> splitter_param_read_pre{};
    std::array<bool, kSources> splitter_matmult_read_pre{};
    std::array<bool, kSources> splitter_write_pre{};
    std::array<Word130, kSources> splitter_word_pre{};
    std::array<bool, kSources> bank_read_pre{};

    for (int source = 0; source < kSources; ++source) {
      const auto& core = *cores[static_cast<size_t>(source)];
      core_param_read_pre[static_cast<size_t>(source)] =
          !rst && core.PE_Param_in_s_read &&
          !core_param_fifo[static_cast<size_t>(source)].empty();
      core_matrix_read_pre[static_cast<size_t>(source)] =
          !rst && core.Matrix_A_Stream_s_read &&
          !core_matrix_fifo[static_cast<size_t>(source)].empty();
      core_x_read_pre[static_cast<size_t>(source)] =
          !rst && core.Vector_X_Stream_in_s_read &&
          !core_x_fifo[static_cast<size_t>(source)].empty();
      core_y_param_write_pre[static_cast<size_t>(source)] =
          !rst && core.Vector_Y_Param_s_write;
      core_y_param_word_pre[static_cast<size_t>(source)] =
          static_cast<Word33>(core.Vector_Y_Param_s_din);
      core_matmult_write_pre[static_cast<size_t>(source)] =
          !rst && core.Matrix_Mult_Vector_Stream_s_write;
      core_matmult_word_pre[static_cast<size_t>(source)] =
          read_word(core.Matrix_Mult_Vector_Stream_s_din);

      const auto& splitter = *splitters[static_cast<size_t>(source)];
      splitter_param_read_pre[static_cast<size_t>(source)] =
          !rst && splitter.Vector_Y_Param_s_read &&
          !splitter_param_fifo[static_cast<size_t>(source)].empty();
      splitter_matmult_read_pre[static_cast<size_t>(source)] =
          !rst && splitter.Matrix_Mult_Vector_Stream_s_read &&
          !splitter_matmult_fifo[static_cast<size_t>(source)].empty();
      splitter_write_pre[static_cast<size_t>(source)] =
          !rst && splitter_lane0_write(splitter);
      splitter_word_pre[static_cast<size_t>(source)] =
          splitter_write_pre[static_cast<size_t>(source)] ?
              splitter_lane0_word(splitter) : Word130{};
      bank_read_pre[static_cast<size_t>(source)] =
          !rst && bank_lane_read(bank, source) &&
          !lane_fifo[static_cast<size_t>(source)].empty();
    }

    const bool bank_write_pre = !rst && bank.Vector_Y_Tagged_Stream_s_write;
    const Word129 bank_word_pre =
        bank_write_pre ? read_word(bank.Vector_Y_Tagged_Stream_s_din)
                       : Word129{};
    const bool scatter_read_pre = !rst && scatter.Vector_Y_Tagged_Stream_0_read;
    const bool scatter_write_pre = !rst && scatter.Y_out_write_addr_s_write;
    const bool scatter_data_write_pre = !rst && scatter.Y_out_write_data_s_write;
    const uint64_t scatter_addr_pre = scatter.Y_out_write_addr_s_din;
    const uint32_t scatter_data_pre =
        static_cast<uint32_t>(scatter.Y_out_write_data_s_din);
    const bool scatter_resp_read_pre = !rst && scatter.Y_out_write_resp_s_read;

    for (auto& core : cores) eval_core(*core, context, 1);
    for (auto& splitter : splitters) eval_splitter(*splitter, context, 1);
    eval_bank(bank, context, 1);
    eval_scatter(scatter, context, 1);

    if (!rst) {
      for (int source = 0; source < kSources; ++source) {
        if (cores[static_cast<size_t>(source)]->ap_done) {
          core_done_seen[static_cast<size_t>(source)] = true;
        }
        if (splitters[static_cast<size_t>(source)]->ap_done) {
          splitter_done_seen[static_cast<size_t>(source)] = true;
        }
      }
      if (bank.ap_done) {
        bank_done_seen = true;
      }
    }

    for (int source = 0; source < kSources; ++source) {
      if (core_param_read_pre[static_cast<size_t>(source)]) {
        core_param_fifo[static_cast<size_t>(source)].pop_front();
        ++core_param_reads;
      }
      if (core_matrix_read_pre[static_cast<size_t>(source)]) {
        core_matrix_fifo[static_cast<size_t>(source)].pop_front();
        ++core_matrix_reads;
      }
      if (core_x_read_pre[static_cast<size_t>(source)]) {
        core_x_fifo[static_cast<size_t>(source)].pop_front();
        ++core_x_reads;
      }
      if (core_y_param_write_pre[static_cast<size_t>(source)]) {
        if (splitter_param_fifo[static_cast<size_t>(source)].size() >= 64) {
          std::cerr << "FAIL: core wrote full splitter param fifo source="
                    << source << "\n";
          return 1;
        }
        splitter_param_fifo[static_cast<size_t>(source)].push_back(
            core_y_param_word_pre[static_cast<size_t>(source)]);
        ++core_y_param_writes;
      }
      if (core_matmult_write_pre[static_cast<size_t>(source)]) {
        if (splitter_matmult_fifo[static_cast<size_t>(source)].size() >= 64) {
          std::cerr << "FAIL: core wrote full splitter matmult fifo source="
                    << source << "\n";
          return 1;
        }
        splitter_matmult_fifo[static_cast<size_t>(source)].push_back(
            core_matmult_word_pre[static_cast<size_t>(source)]);
        ++core_matmult_writes;
      }
      if (splitter_param_read_pre[static_cast<size_t>(source)]) {
        splitter_param_fifo[static_cast<size_t>(source)].pop_front();
        ++splitter_param_reads;
      }
      if (splitter_matmult_read_pre[static_cast<size_t>(source)]) {
        splitter_matmult_fifo[static_cast<size_t>(source)].pop_front();
        ++splitter_matmult_reads;
      }
      if (splitter_write_pre[static_cast<size_t>(source)]) {
        if (lane_fifo[static_cast<size_t>(source)].size() >= 64) {
          std::cerr << "FAIL: splitter wrote full lane fifo source="
                    << source << "\n";
          return 1;
        }
        lane_fifo[static_cast<size_t>(source)].push_back(
            splitter_word_pre[static_cast<size_t>(source)]);
        ++splitter_writes;
      }
      if (bank_read_pre[static_cast<size_t>(source)]) {
        lane_fifo[static_cast<size_t>(source)].pop_front();
        ++bank_reads;
      }
    }

    if (bank_write_pre) {
      if (bank_to_scatter_fifo.size() >= 64) {
        std::cerr << "FAIL: bank wrote full scatter fifo\n";
        return 1;
      }
      bank_to_scatter_fifo.push_back(bank_word_pre);
      ++bank_pairs;
    }
    if (scatter_read_pre) {
      if (bank_to_scatter_fifo.empty()) {
        std::cerr << "FAIL: scatter read empty fifo\n";
        return 1;
      }
      bank_to_scatter_fifo.pop_front();
      ++scatter_reads;
    }
    if (scatter_write_pre) {
      if (!scatter_data_write_pre) {
        std::cerr << "FAIL: addr write without data write\n";
        return 1;
      }
      const uint64_t addr = scatter_addr_pre >> 2;
      if (addr >= y.size()) {
        std::cerr << "FAIL: bad addr=" << scatter_addr_pre << "\n";
        return 1;
      }
      y[addr] = scatter_data_pre;
      seen[addr] = true;
      ++writes;
      ++pending_responses;
    }
    if (scatter_resp_read_pre) {
      if (!response_available) {
        std::cerr << "FAIL: response read on empty stream\n";
        return 1;
      }
      --pending_responses;
    }

    if (!rst && scatter.ap_done) {
      scatter_done_seen = true;
      int errors = 0;
      for (int source = 0; source < kSources; ++source) {
        if (!core_param_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " core param left="
                    << core_param_fifo[static_cast<size_t>(source)].size()
                    << "\n";
          ++errors;
        }
        if (!core_matrix_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " core matrix left="
                    << core_matrix_fifo[static_cast<size_t>(source)].size()
                    << "\n";
          ++errors;
        }
        if (!core_x_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " core x left="
                    << core_x_fifo[static_cast<size_t>(source)].size()
                    << "\n";
          ++errors;
        }
        if (!splitter_param_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " splitter param left="
                    << splitter_param_fifo[static_cast<size_t>(source)].size()
                    << "\n";
          ++errors;
        }
        if (!splitter_matmult_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " splitter matmult left="
                    << splitter_matmult_fifo[static_cast<size_t>(source)].size()
                    << "\n";
          ++errors;
        }
        if (!lane_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " lane fifo left="
                    << lane_fifo[static_cast<size_t>(source)].size() << "\n";
          ++errors;
        }
      }

      const int expected_param_reads = 0
          + (4 + (16 - 0) * 2)
          + (4 + (16 - 2) * 2)
          + (4 + (16 - 4) * 2)
          + (4 + (16 - 6) * 2)
          + (4 + (16 - 8) * 2)
          + (4 + (16 - 10) * 2)
          + (4 + (16 - 12) * 2)
          + (4 + (16 - 14) * 2);
      if (core_param_reads != expected_param_reads) {
        std::cerr << "FAIL: core_param_reads=" << core_param_reads
                  << " expect=" << expected_param_reads << "\n";
        ++errors;
      }
      if (core_x_reads != kSources) {
        std::cerr << "FAIL: core_x_reads=" << core_x_reads
                  << " expect=" << kSources << "\n";
        ++errors;
      }
      if (core_matrix_reads != kSources * kPacketsPerSource) {
        std::cerr << "FAIL: core_matrix_reads=" << core_matrix_reads
                  << " expect=" << kSources * kPacketsPerSource << "\n";
        ++errors;
      }
      if (core_y_param_writes != kSources * 5) {
        std::cerr << "FAIL: core_y_param_writes=" << core_y_param_writes
                  << " expect=" << kSources * 5 << "\n";
        ++errors;
      }
      if (core_matmult_writes != kSources * kPacketsPerSource) {
        std::cerr << "FAIL: core_matmult_writes=" << core_matmult_writes
                  << " expect=" << kSources * kPacketsPerSource << "\n";
        ++errors;
      }
      if (splitter_param_reads != core_y_param_writes) {
        std::cerr << "FAIL: splitter_param_reads=" << splitter_param_reads
                  << " core_y_param_writes=" << core_y_param_writes << "\n";
        ++errors;
      }
      if (splitter_matmult_reads != core_matmult_writes) {
        std::cerr << "FAIL: splitter_matmult_reads=" << splitter_matmult_reads
                  << " core_matmult_writes=" << core_matmult_writes << "\n";
        ++errors;
      }
      if (splitter_writes != kSources * (kPacketsPerSource + 1)) {
        std::cerr << "FAIL: splitter_writes=" << splitter_writes
                  << " expect=" << kSources * (kPacketsPerSource + 1)
                  << "\n";
        ++errors;
      }
      if (bank_reads != splitter_writes) {
        std::cerr << "FAIL: bank_reads=" << bank_reads
                  << " splitter_writes=" << splitter_writes << "\n";
        ++errors;
      }
      if (bank_pairs != kPairs) {
        std::cerr << "FAIL: bank_pairs=" << bank_pairs
                  << " expect=" << kPairs << "\n";
        ++errors;
      }
      if (scatter_reads != kPairs) {
        std::cerr << "FAIL: scatter_reads=" << scatter_reads
                  << " expect=" << kPairs << "\n";
        ++errors;
      }
      if (writes != kScalars) {
        std::cerr << "FAIL: writes=" << writes
                  << " expect=" << kScalars << "\n";
        ++errors;
      }
      for (int i = 0; i < kScalars; ++i) {
        if (!seen[static_cast<size_t>(i)] ||
            y[static_cast<size_t>(i)] != expected_value(i)) {
          std::cerr << "FAIL: y[" << i << "] seen="
                    << seen[static_cast<size_t>(i)]
                    << " got=0x" << std::hex << y[static_cast<size_t>(i)]
                    << " expect=0x" << expected_value(i) << std::dec
                    << " got_float=" << fval(y[static_cast<size_t>(i)])
                    << "\n";
          ++errors;
        }
      }
      if (errors != 0) {
        return 1;
      }

      std::cout << "PASS: core-splitter-bank-generated-scatter cpp harness"
                << " cycles=" << cycle
                << " core_param_reads=" << core_param_reads
                << " core_matrix_reads=" << core_matrix_reads
                << " core_x_reads=" << core_x_reads
                << " core_matmult_writes=" << core_matmult_writes
                << " splitter_writes=" << splitter_writes
                << " bank_pairs=" << bank_pairs
                << " scatter_reads=" << scatter_reads
                << " writes=" << writes << "\n";
      for (auto& core : cores) core->final();
      for (auto& splitter : splitters) splitter->final();
      bank.final();
      scatter.final();
      return 0;
    }
  }

  std::cerr << "FAIL: timeout"
            << " core_param_reads=" << core_param_reads
            << " core_matrix_reads=" << core_matrix_reads
            << " core_x_reads=" << core_x_reads
            << " core_y_param_writes=" << core_y_param_writes
            << " core_matmult_writes=" << core_matmult_writes
            << " splitter_param_reads=" << splitter_param_reads
            << " splitter_matmult_reads=" << splitter_matmult_reads
            << " splitter_writes=" << splitter_writes
            << " bank_reads=" << bank_reads
            << " bank_pairs=" << bank_pairs
            << " scatter_reads=" << scatter_reads
            << " writes=" << writes
            << " bank_to_scatter_fifo=" << bank_to_scatter_fifo.size()
            << " pending_responses=" << pending_responses << "\n";
  for (int i = 0; i < kScalars; ++i) {
    if (!seen[static_cast<size_t>(i)] ||
        y[static_cast<size_t>(i)] != expected_value(i)) {
      std::cerr << "  y[" << i << "] seen=" << seen[static_cast<size_t>(i)]
                << " got=0x" << std::hex << y[static_cast<size_t>(i)]
                << " expect=0x" << expected_value(i) << std::dec
                << " got_float=" << fval(y[static_cast<size_t>(i)]) << "\n";
    }
  }
  return 1;
}
