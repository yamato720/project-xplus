#include "VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.h"
#include "VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.h"

#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

namespace {

constexpr int kLanes = 8;
constexpr int kLaneWords = 3;
constexpr int kPairs = 8;
constexpr int kScalars = 16;

uint32_t fbits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::array<uint32_t, 5> pack_scalar(bool done,
                                    uint32_t packet_idx,
                                    uint32_t pair_lane,
                                    uint32_t scalar_lane,
                                    uint32_t value) {
  // TaggedScalar layout:
  // {pad[129], value[128:97], scalar_lane[96:65],
  //  pair_lane[64:33], packet_idx[32:1], done[0]}.
  std::array<uint32_t, 5> word{};
  word[0] = (packet_idx << 1) | (done ? 1U : 0U);
  word[1] = (packet_idx >> 31) | (pair_lane << 1);
  word[2] = (pair_lane >> 31) | (scalar_lane << 1);
  word[3] = (scalar_lane >> 31) | (value << 1);
  word[4] = value >> 31;
  return word;
}

void drive_word(VlWide<5>& port, const std::array<uint32_t, 5>& word) {
  for (int i = 0; i < 5; ++i) {
    port[i] = word[static_cast<size_t>(i)];
  }
}

std::array<uint32_t, 5> read_word(const VlWide<5>& port) {
  std::array<uint32_t, 5> word{};
  for (int i = 0; i < 5; ++i) {
    word[static_cast<size_t>(i)] = port[i];
  }
  return word;
}

uint32_t expected_value(int idx) {
  if ((idx & 1) == 0) {
    return fbits(10.0f + static_cast<float>(idx >> 1));
  }
  return fbits(100.0f + static_cast<float>(idx >> 1));
}

void eval_bank(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank,
               VerilatedContext& context,
               int clk) {
  bank.ap_clk = clk;
  bank.eval();
  context.timeInc(5);
}

void eval_scatter(VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel& scatter,
                  VerilatedContext& context,
                  int clk) {
  scatter.ap_clk = clk;
  scatter.eval();
  context.timeInc(5);
}

void clear_bank_peek_ports(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank) {
  std::array<uint32_t, 5> zero{};
  drive_word(bank.Owner_Lane_Stream_0_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_1_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_2_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_3_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_4_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_5_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_6_peek_dout, zero);
  drive_word(bank.Owner_Lane_Stream_7_peek_dout, zero);
  bank.Owner_Lane_Stream_0_peek_empty_n = 0;
  bank.Owner_Lane_Stream_1_peek_empty_n = 0;
  bank.Owner_Lane_Stream_2_peek_empty_n = 0;
  bank.Owner_Lane_Stream_3_peek_empty_n = 0;
  bank.Owner_Lane_Stream_4_peek_empty_n = 0;
  bank.Owner_Lane_Stream_5_peek_empty_n = 0;
  bank.Owner_Lane_Stream_6_peek_empty_n = 0;
  bank.Owner_Lane_Stream_7_peek_empty_n = 0;
  std::array<uint32_t, 5> zero_out{};
  drive_word(bank.Vector_Y_Tagged_Stream_peek, zero_out);
}

void drive_lane(VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank,
                int lane,
                const std::vector<std::array<uint32_t, 5>>& words,
                size_t idx) {
  const bool valid = idx < words.size();
  std::array<uint32_t, 5> zero{};
  const auto& word = valid ? words[idx] : zero;
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

bool lane_read(const VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo& bank, int lane) {
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

void clear_scatter_unused(VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel& scatter) {
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

  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo bank;
  VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel scatter;

  std::array<std::vector<std::array<uint32_t, 5>>, kLanes> lane_words;
  std::array<size_t, kLanes> lane_idx{};
  for (int lane = 0; lane < kLanes; ++lane) {
    lane_words[static_cast<size_t>(lane)].push_back(
        pack_scalar(false, 0, static_cast<uint32_t>(lane), 0,
                    fbits(10.0f + static_cast<float>(lane))));
    lane_words[static_cast<size_t>(lane)].push_back(
        pack_scalar(false, 0, static_cast<uint32_t>(lane), 1,
                    fbits(100.0f + static_cast<float>(lane))));
    lane_words[static_cast<size_t>(lane)].push_back(
        pack_scalar(true, 0, static_cast<uint32_t>(lane), 0, 0));
  }

  std::deque<std::array<uint32_t, 5>> fifo;
  std::array<uint32_t, kScalars> y{};
  std::array<bool, kScalars> seen{};
  int pending_responses = 0;
  int bank_pairs = 0;
  int scatter_reads = 0;
  int writes = 0;

  for (uint64_t cycle = 0; cycle < 2000; ++cycle) {
    const bool rst = cycle < 5;
    const bool response_available = !rst && pending_responses > 0;

    bank.ap_rst_n = rst ? 0 : 1;
    bank.ap_start = (cycle == 5) ? 1 : 0;
    bank.Iteration_num = 1;
    bank.Row_num = 16;
    bank.Owner_id = 0;
    bank.Vector_Y_Tagged_Stream_s_full_n = fifo.size() < 64 ? 1 : 0;
    clear_bank_peek_ports(bank);
    for (int lane = 0; lane < kLanes; ++lane) {
      drive_lane(bank,
                 lane,
                 lane_words[static_cast<size_t>(lane)],
                 lane_idx[static_cast<size_t>(lane)]);
    }

    scatter.ap_rst = rst ? 1 : 0;
    scatter.ap_start = (cycle == 5) ? 1 : 0;
    scatter.scalar_writes_total = kScalars;
    scatter.tagged_pairs_total = kPairs;
    scatter.Y_out_write_addr_s_full_n = 1;
    scatter.Y_out_write_data_s_full_n = 1;
    scatter.Y_out_write_addr_offset_load = 0;
    scatter.Y_out_write_resp_s_dout = 0;
    scatter.Y_out_write_resp_s_empty_n = response_available ? 1 : 0;
    clear_scatter_unused(scatter);
    if (!rst && !fifo.empty()) {
      drive_word(scatter.Vector_Y_Tagged_Stream_0_dout, fifo.front());
      scatter.Vector_Y_Tagged_Stream_0_empty_n = 1;
    } else {
      std::array<uint32_t, 5> zero{};
      drive_word(scatter.Vector_Y_Tagged_Stream_0_dout, zero);
      scatter.Vector_Y_Tagged_Stream_0_empty_n = 0;
    }

    eval_bank(bank, context, 0);
    std::array<bool, kLanes> lane_read_pre{};
    for (int lane = 0; lane < kLanes; ++lane) {
      lane_read_pre[static_cast<size_t>(lane)] =
          !rst &&
          lane_idx[static_cast<size_t>(lane)] <
              lane_words[static_cast<size_t>(lane)].size() &&
          lane_read(bank, lane);
    }
    const bool bank_write_pre = !rst && bank.Vector_Y_Tagged_Stream_s_write;
    const std::array<uint32_t, 5> bank_word_pre =
        bank_write_pre ? read_word(bank.Vector_Y_Tagged_Stream_s_din)
                       : std::array<uint32_t, 5>{};
    eval_scatter(scatter, context, 0);
    eval_bank(bank, context, 1);
    eval_scatter(scatter, context, 1);

    for (int lane = 0; lane < kLanes; ++lane) {
      const size_t idx = lane_idx[static_cast<size_t>(lane)];
      if (lane_read_pre[static_cast<size_t>(lane)]) {
        lane_idx[static_cast<size_t>(lane)] = idx + 1;
      }
    }
    if (bank_write_pre) {
      if (fifo.size() >= 64) {
        std::cerr << "FAIL: bank wrote into full fifo\n";
        return 1;
      }
      fifo.push_back(bank_word_pre);
      ++bank_pairs;
    }
    if (!rst && scatter.Vector_Y_Tagged_Stream_0_read) {
      if (fifo.empty()) {
        std::cerr << "FAIL: scatter read empty fifo\n";
        return 1;
      }
      fifo.pop_front();
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

    if (!rst && scatter.ap_done) {
      int errors = 0;
      for (int lane = 0; lane < kLanes; ++lane) {
        if (lane_idx[static_cast<size_t>(lane)] !=
            lane_words[static_cast<size_t>(lane)].size()) {
          std::cerr << "FAIL: lane " << lane << " consumed "
                    << lane_idx[static_cast<size_t>(lane)] << " of "
                    << lane_words[static_cast<size_t>(lane)].size() << "\n";
          ++errors;
        }
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
      std::cout << "PASS: bank-to-scatter cpp harness cycles=" << cycle
                << " bank_pairs=" << bank_pairs
                << " scatter_reads=" << scatter_reads
                << " writes=" << writes << "\n";
      bank.final();
      scatter.final();
      return 0;
    }
  }

  std::cerr << "FAIL: timeout bank_pairs=" << bank_pairs
            << " scatter_reads=" << scatter_reads
            << " writes=" << writes
            << " fifo=" << fifo.size()
            << " pending_responses=" << pending_responses << "\n";
  return 1;
}
