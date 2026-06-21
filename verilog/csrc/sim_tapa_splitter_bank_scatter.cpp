#include "VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.h"
#include "VCuperSpmvOnly_SourceLaneSplitterOoo.h"
#ifdef USE_TAPA_GENERATED_SCATTER
#include "VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.h"
#else
#include "VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.h"
#endif

#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr int kSources = 8;
constexpr int kLanes = 8;
constexpr int kPacketsPerSource = 2;
constexpr int kPairs = 8;
constexpr int kScalars = 16;

using Word33 = uint64_t;
using Word130 = std::array<uint32_t, 5>;
using Word129 = std::array<uint32_t, 5>;
using Word401 = std::array<uint32_t, 13>;

#ifdef USE_TAPA_GENERATED_SCATTER
using ScatterTop =
    VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter;
constexpr const char* kHarnessName = "splitter-bank-generated-scatter";
#else
using ScatterTop = VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel;
constexpr const char* kHarnessName = "splitter-bank-scatter-model";
#endif

uint32_t fbits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

Word33 pack_u33(uint32_t value) { return value; }

Word401 pack_matrix_mult_packet(uint32_t row0, uint32_t value0) {
  // Matrix_Mult_X 的 RTL bit layout:
  //   row[0..7] 各 18 bit，位于 [0..143]
  //   val[0..7] 各 32 bit，位于 [144..399]
  //   bit[400] 是 HLS 结构体 pad。
  // 这里只让 lane0 有效，lane1..7 标成 padding。
  Word401 word{};
  auto set_bits = [&](int lo, int width, uint32_t value) {
    for (int bit = 0; bit < width; ++bit) {
      if ((value >> bit) & 1U) {
        const int pos = lo + bit;
        word[static_cast<size_t>(pos / 32)] |=
            1U << static_cast<unsigned>(pos % 32);
      }
    }
  };

  set_bits(0, 18, row0 & 0x3FFFFU);
  for (int lane = 1; lane < kLanes; ++lane) {
    set_bits(lane * 18, 18, 1U << 17);
  }
  set_bits(144, 32, value0);
  return word;
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

uint32_t expected_value(int idx) {
  if ((idx & 1) == 0) {
    return fbits(10.0f + static_cast<float>(idx >> 1));
  }
  return fbits(100.0f + static_cast<float>(idx >> 1));
}

void eval_splitter(VCuperSpmvOnly_SourceLaneSplitterOoo& splitter,
                   VerilatedContext& context,
                   int clk) {
  splitter.ap_clk = clk;
  splitter.eval();
  context.timeInc(1);
}

void eval_bank(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank,
               VerilatedContext& context,
               int clk) {
  bank.ap_clk = clk;
  bank.eval();
  context.timeInc(1);
}

void eval_scatter(ScatterTop& scatter, VerilatedContext& context, int clk) {
  scatter.ap_clk = clk;
  scatter.eval();
  context.timeInc(1);
}

void clear_splitter_unused_inputs(VCuperSpmvOnly_SourceLaneSplitterOoo& splitter) {
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

void drive_splitter_params(VCuperSpmvOnly_SourceLaneSplitterOoo& splitter,
                           const std::deque<Word33>& fifo) {
  if (!fifo.empty()) {
    splitter.Vector_Y_Param_s_dout = fifo.front();
    splitter.Vector_Y_Param_s_empty_n = 1;
  } else {
    Word33 zero{};
    splitter.Vector_Y_Param_s_dout = zero;
    splitter.Vector_Y_Param_s_empty_n = 0;
  }
}

void drive_splitter_matrix(VCuperSpmvOnly_SourceLaneSplitterOoo& splitter,
                           const std::deque<Word401>& fifo) {
  if (!fifo.empty()) {
    drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout, fifo.front());
    splitter.Matrix_Mult_Vector_Stream_s_empty_n = 1;
  } else {
    Word401 zero{};
    drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout, zero);
    splitter.Matrix_Mult_Vector_Stream_s_empty_n = 0;
  }
}

bool splitter_lane0_write(const VCuperSpmvOnly_SourceLaneSplitterOoo& splitter) {
  return splitter.Owner_Lane_Stream_0_s_write;
}

Word130 splitter_lane0_word(const VCuperSpmvOnly_SourceLaneSplitterOoo& splitter) {
  return read_word(splitter.Owner_Lane_Stream_0_s_din);
}

void clear_bank_peek_ports(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank) {
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

void drive_bank_lane(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank,
                     int lane,
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

bool bank_lane_read(const VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank, int lane) {
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

  std::array<std::unique_ptr<VCuperSpmvOnly_SourceLaneSplitterOoo>, kSources> splitters;
  for (int i = 0; i < kSources; ++i) {
    splitters[static_cast<size_t>(i)] =
        std::make_unique<VCuperSpmvOnly_SourceLaneSplitterOoo>();
  }
  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo bank;
  ScatterTop scatter;

  std::array<std::deque<Word33>, kSources> param_fifo;
  std::array<std::deque<Word401>, kSources> matrix_fifo;
  std::array<std::deque<Word130>, kSources> lane_fifo;
  std::deque<Word129> bank_to_scatter_fifo;

  for (int source = 0; source < kSources; ++source) {
    param_fifo[static_cast<size_t>(source)].push_back(pack_u33(1));   // Batch_num
    param_fifo[static_cast<size_t>(source)].push_back(pack_u33(16));  // Row_num
    param_fifo[static_cast<size_t>(source)].push_back(pack_u33(1));   // Iteration_num
    param_fifo[static_cast<size_t>(source)].push_back(pack_u33(0));   // start_32
    param_fifo[static_cast<size_t>(source)].push_back(pack_u33(2));   // end_32

    matrix_fifo[static_cast<size_t>(source)].push_back(
        pack_matrix_mult_packet(0, fbits(10.0f + static_cast<float>(source))));
    matrix_fifo[static_cast<size_t>(source)].push_back(
        pack_matrix_mult_packet(1, fbits(100.0f + static_cast<float>(source))));
  }

  std::array<uint32_t, kScalars> y{};
  std::array<bool, kScalars> seen{};
  int splitter_writes = 0;
  int bank_reads = 0;
  int bank_pairs = 0;
  int scatter_reads = 0;
  int writes = 0;
  int pending_responses = 0;
  bool scatter_done_seen = false;

  for (uint64_t cycle = 0; cycle < 5000; ++cycle) {
    const bool rst = cycle < 5;
    const bool response_available = !rst && pending_responses > 0;

    for (int source = 0; source < kSources; ++source) {
      auto& splitter = *splitters[static_cast<size_t>(source)];
      splitter.ap_rst_n = rst ? 0 : 1;
      splitter.ap_start = (cycle == 5) ? 1 : 0;
      splitter.Source_id = static_cast<uint32_t>(source * 2);
      clear_splitter_unused_inputs(splitter);
      drive_splitter_params(splitter, param_fifo[static_cast<size_t>(source)]);
      drive_splitter_matrix(splitter, matrix_fifo[static_cast<size_t>(source)]);
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
    bank.ap_start = (cycle == 5) ? 1 : 0;
    bank.Iteration_num = 1;
    bank.Row_num = 16;
    bank.Owner_id = 0;
    bank.Vector_Y_Tagged_Stream_s_full_n =
        bank_to_scatter_fifo.size() < 64 ? 1 : 0;
    clear_bank_peek_ports(bank);
    for (int lane = 0; lane < kSources; ++lane) {
      drive_bank_lane(bank, lane, lane_fifo[static_cast<size_t>(lane)]);
    }

    scatter.ap_rst = rst ? 1 : 0;
#ifdef USE_TAPA_GENERATED_SCATTER
    // Vitis HLS pipeline modules gate each II with ap_start_int, so a single
    // cycle start pulse can stop the generated scatter before bank data arrives.
    // TAPA keeps task start live for the task lifetime; mirror that here.
    scatter.ap_start = (!rst && !scatter_done_seen) ? 1 : 0;
#else
    scatter.ap_start = (cycle == 5) ? 1 : 0;
#endif
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

    for (auto& splitter : splitters) eval_splitter(*splitter, context, 0);
    eval_bank(bank, context, 0);
    eval_scatter(scatter, context, 0);

    std::array<bool, kSources> param_read_pre{};
    std::array<bool, kSources> matrix_read_pre{};
    std::array<bool, kSources> splitter_write_pre{};
    std::array<Word130, kSources> splitter_word_pre{};
    std::array<bool, kSources> bank_read_pre{};
    for (int source = 0; source < kSources; ++source) {
      const auto& splitter = *splitters[static_cast<size_t>(source)];
      param_read_pre[static_cast<size_t>(source)] =
          !rst && splitter.Vector_Y_Param_s_read &&
          !param_fifo[static_cast<size_t>(source)].empty();
      matrix_read_pre[static_cast<size_t>(source)] =
          !rst && splitter.Matrix_Mult_Vector_Stream_s_read &&
          !matrix_fifo[static_cast<size_t>(source)].empty();
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
#ifdef USE_TAPA_GENERATED_SCATTER
    const bool scatter_read_pre = !rst && scatter.Vector_Y_Tagged_Stream_0_read;
    const bool scatter_write_pre = !rst && scatter.Y_out_write_addr_s_write;
    const bool scatter_data_write_pre = !rst && scatter.Y_out_write_data_s_write;
    const uint64_t scatter_addr_pre = scatter.Y_out_write_addr_s_din;
    const uint32_t scatter_data_pre =
        static_cast<uint32_t>(scatter.Y_out_write_data_s_din);
    const bool scatter_resp_read_pre = !rst && scatter.Y_out_write_resp_s_read;
#endif
    for (auto& splitter : splitters) eval_splitter(*splitter, context, 1);
    eval_bank(bank, context, 1);
    eval_scatter(scatter, context, 1);

    for (int source = 0; source < kSources; ++source) {
      if (param_read_pre[static_cast<size_t>(source)]) {
        param_fifo[static_cast<size_t>(source)].pop_front();
      }
      if (matrix_read_pre[static_cast<size_t>(source)]) {
        matrix_fifo[static_cast<size_t>(source)].pop_front();
      }
      if (splitter_write_pre[static_cast<size_t>(source)]) {
        if (lane_fifo[static_cast<size_t>(source)].size() >= 64) {
          std::cerr << "FAIL: splitter wrote into full lane fifo source="
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
        std::cerr << "FAIL: bank wrote into full scatter fifo\n";
        return 1;
      }
      bank_to_scatter_fifo.push_back(bank_word_pre);
      ++bank_pairs;
    }
#ifdef USE_TAPA_GENERATED_SCATTER
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
#else
    if (!rst && scatter.Vector_Y_Tagged_Stream_0_read) {
      if (bank_to_scatter_fifo.empty()) {
        std::cerr << "FAIL: scatter read empty fifo\n";
        return 1;
      }
      bank_to_scatter_fifo.pop_front();
      ++scatter_reads;
    }
    if (!rst && scatter.Y_out_write_addr_s_write) {
      if (!scatter.Y_out_write_data_s_write) {
        std::cerr << "FAIL: addr write without data write\n";
        return 1;
      }
      const uint64_t addr = scatter.Y_out_write_addr_s_din >> 2;
      if (addr >= y.size()) {
        std::cerr << "FAIL: bad addr=" << scatter.Y_out_write_addr_s_din << "\n";
        return 1;
      }
      y[addr] = static_cast<uint32_t>(scatter.Y_out_write_data_s_din);
      seen[addr] = true;
      ++writes;
      ++pending_responses;
    }
    if (!rst && scatter.Y_out_write_resp_s_read) {
      if (!response_available) {
        std::cerr << "FAIL: response read on empty stream\n";
        return 1;
      }
      --pending_responses;
    }
#endif

    if (!rst && scatter.ap_done) {
      scatter_done_seen = true;
      int errors = 0;
      for (int source = 0; source < kSources; ++source) {
        if (!param_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " param fifo left="
                    << param_fifo[static_cast<size_t>(source)].size() << "\n";
          ++errors;
        }
        if (!matrix_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " matrix fifo left="
                    << matrix_fifo[static_cast<size_t>(source)].size() << "\n";
          ++errors;
        }
        if (!lane_fifo[static_cast<size_t>(source)].empty()) {
          std::cerr << "FAIL: source " << source << " lane fifo left="
                    << lane_fifo[static_cast<size_t>(source)].size() << "\n";
          ++errors;
        }
      }
      if (splitter_writes != kSources * (kPacketsPerSource + 1)) {
        std::cerr << "FAIL: splitter_writes=" << splitter_writes
                  << " expect=" << kSources * (kPacketsPerSource + 1) << "\n";
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
                    << " expect=0x" << expected_value(i) << std::dec << "\n";
          ++errors;
        }
      }
      if (errors != 0) {
        return 1;
      }
      std::cout << "PASS: " << kHarnessName << " cpp harness cycles=" << cycle
                << " splitter_writes=" << splitter_writes
                << " bank_reads=" << bank_reads
                << " bank_pairs=" << bank_pairs
                << " scatter_reads=" << scatter_reads
                << " writes=" << writes << "\n";
      for (auto& splitter : splitters) splitter->final();
      bank.final();
      scatter.final();
      return 0;
    }
  }

  std::cerr << "FAIL: timeout splitter_writes=" << splitter_writes
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
                << " expect=0x" << expected_value(i) << std::dec << "\n";
    }
  }
  return 1;
}
