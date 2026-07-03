#include "VCuperSpmvOnly_ChiselDataPath8.h"

#include "verilated.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kChannels = 8;
constexpr int kWideWords = 17;
constexpr int kTaggedWords = 5;
constexpr int kColumns = 1024;

using Wide513 = std::array<uint32_t, kWideWords>;
using Tagged129 = std::array<uint32_t, kTaggedWords>;

struct SlotSpec {
  int row = 0;
  int col = 0;
  float value = 0.0f;
  bool padding = true;
  bool reuse = false;
};

struct SimCase {
  std::string name;
  int rows = 128;
  int iteration_num = 1;
  std::vector<std::array<std::vector<Wide513>, kChannels>> batches;
};

struct CaseResult {
  int tagged_count = 0;
  uint64_t cycle_count = 0;
};

uint32_t float_bits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float bits_float(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t float_result_bits(float value) {
  return float_bits(value);
}

template <size_t N>
void drive_wide(VlWide<N>& port, const std::array<uint32_t, N>& value) {
  for (int i = 0; i < static_cast<int>(N); ++i) {
    port[i] = value[static_cast<size_t>(i)];
  }
}

template <size_t N>
std::array<uint32_t, N> read_wide(const VlWide<N>& port) {
  std::array<uint32_t, N> value{};
  for (int i = 0; i < static_cast<int>(N); ++i) {
    value[static_cast<size_t>(i)] = port[i];
  }
  return value;
}

Wide513 zero_wide() {
  return {};
}

int num_packets(int count) {
  return (count + 15) / 16;
}

int num_groups(int rows) {
  return (num_packets(rows) + kChannels - 1) / kChannels;
}

uint32_t encoded_row(int packet, int scalar_lane) {
  return static_cast<uint32_t>((packet >> 3) * 2 + (scalar_lane & 1));
}

Wide513 pack_x_packet(int packet_idx) {
  Wide513 word{};
  for (int lane = 0; lane < 16; ++lane) {
    const int col = packet_idx * 16 + lane;
    word[static_cast<size_t>(lane)] = float_bits(static_cast<float>(col + 1));
  }
  return word;
}

Wide513 pack_matrix_beat(const std::array<SlotSpec, kChannels>& slots) {
  Wide513 beat{};
  for (int lane = 0; lane < kChannels; ++lane) {
    const SlotSpec& slot = slots[static_cast<size_t>(lane)];
    const uint32_t row = slot.padding ? (1U << 17) : static_cast<uint32_t>(slot.row);
    const uint32_t col = slot.reuse ? 0x3fffU : static_cast<uint32_t>(slot.col);
    const uint32_t value_bits = float_bits(slot.value);
    beat[static_cast<size_t>(lane * 2)] = value_bits;
    beat[static_cast<size_t>(lane * 2 + 1)] =
        (row & 0x3ffffU) | ((col & 0x3fffU) << 18);
  }
  return beat;
}

std::array<SlotSpec, kChannels> padded_slots() {
  std::array<SlotSpec, kChannels> slots{};
  for (auto& slot : slots) {
    slot.padding = true;
  }
  return slots;
}

void set_slot(std::array<SlotSpec, kChannels>& slots,
              int owner,
              int packet,
              int scalar_lane,
              int col,
              float value) {
  SlotSpec& slot = slots[static_cast<size_t>(owner)];
  slot.row = static_cast<int>(encoded_row(packet, scalar_lane));
  slot.col = col;
  slot.value = value;
  slot.padding = false;
  slot.reuse = false;
}

void set_reuse_slot(std::array<SlotSpec, kChannels>& slots,
                    int owner,
                    int packet,
                    int scalar_lane) {
  SlotSpec& slot = slots[static_cast<size_t>(owner)];
  slot.row = static_cast<int>(encoded_row(packet, scalar_lane));
  slot.col = 0x3fff;
  slot.value = 0.0f;
  slot.padding = false;
  slot.reuse = true;
}

std::vector<Wide513> x_packets_for_case(const SimCase& sim_case) {
  const int packets = num_packets(kColumns);
  std::vector<Wide513> packets_out;
  packets_out.reserve(static_cast<size_t>(packets) *
                      static_cast<size_t>(sim_case.batches.size()) *
                      static_cast<size_t>(std::max(1, sim_case.iteration_num)));
  for (int iter = 0; iter < std::max(1, sim_case.iteration_num); ++iter) {
    (void)iter;
    for (size_t batch = 0; batch < sim_case.batches.size(); ++batch) {
      (void)batch;
      for (int packet = 0; packet < packets; ++packet) {
        packets_out.push_back(pack_x_packet(packet));
      }
    }
  }
  return packets_out;
}

void accumulate_expected_for_slot(
    const SlotSpec& slot,
    int source,
    int owner,
    std::vector<std::array<std::array<float, 2>, kChannels>>& expected,
    uint32_t& col_old,
    float& val_old) {
  if (slot.padding) {
    return;
  }

  const uint32_t row = static_cast<uint32_t>(slot.row);
  uint32_t col = static_cast<uint32_t>(slot.col);
  float value = slot.value;
  if ((col_old & col) == 0x3fffU) {
    value = val_old;
  } else {
    val_old = value;
  }
  col_old = col;

  const int group = static_cast<int>(row >> 1);
  const int scalar_lane = static_cast<int>(row & 1U);
  const int packet = group * kChannels + owner;
  if (packet >= static_cast<int>(expected.size())) {
    return;
  }
  const int col_idx = static_cast<int>(col);
  const float x = static_cast<float>(col_idx + 1);
  expected[static_cast<size_t>(packet)][static_cast<size_t>(source)]
          [static_cast<size_t>(scalar_lane)] += value * x;
}

std::vector<std::array<std::array<float, 2>, kChannels>>
expected_outputs(const SimCase& sim_case) {
  const int packets = num_packets(sim_case.rows);
  std::vector<std::array<std::array<float, 2>, kChannels>> expected(
      static_cast<size_t>(packets));
  for (auto& packet : expected) {
    for (auto& pair : packet) {
      pair = {0.0f, 0.0f};
    }
  }

  for (int iter = 0; iter < std::max(1, sim_case.iteration_num); ++iter) {
    (void)iter;
    for (const auto& batch : sim_case.batches) {
      for (int source = 0; source < kChannels; ++source) {
        for (const Wide513& beat : batch[static_cast<size_t>(source)]) {
          uint32_t col_old = 0x3fffU;
          float val_old = 0.0f;
          for (int owner = 0; owner < kChannels; ++owner) {
            const uint32_t raw_val = beat[static_cast<size_t>(owner * 2)];
            const uint32_t raw_meta = beat[static_cast<size_t>(owner * 2 + 1)];
            SlotSpec slot;
            slot.value = bits_float(raw_val);
            slot.row = static_cast<int>(raw_meta & 0x3ffffU);
            slot.col = static_cast<int>((raw_meta >> 18) & 0x3fffU);
            slot.padding = ((slot.row >> 17) & 1) != 0;
            accumulate_expected_for_slot(
                slot, source, owner, expected, col_old, val_old);
          }
        }
      }
    }
  }
  return expected;
}

std::deque<uint64_t> pe_stream_for_case(const SimCase& sim_case) {
  std::deque<uint64_t> pe;
  pe.push_back(static_cast<uint64_t>(sim_case.batches.size()));
  pe.push_back(static_cast<uint64_t>(sim_case.rows));
  pe.push_back(static_cast<uint64_t>(sim_case.iteration_num));
  pe.push_back(kColumns);

  for (int iter = 0; iter < std::max(1, sim_case.iteration_num); ++iter) {
    (void)iter;
    std::array<uint32_t, kChannels> start{};
    for (const auto& batch : sim_case.batches) {
      for (int channel = 0; channel < kChannels; ++channel) {
        pe.push_back(start[static_cast<size_t>(channel)]);
      }
      for (int channel = 0; channel < kChannels; ++channel) {
        start[static_cast<size_t>(channel)] +=
            static_cast<uint32_t>(batch[static_cast<size_t>(channel)].size());
        pe.push_back(start[static_cast<size_t>(channel)]);
      }
    }
  }
  return pe;
}

std::array<std::deque<Wide513>, kChannels> matrix_streams_for_case(
    const SimCase& sim_case) {
  std::array<std::deque<Wide513>, kChannels> matrix;
  for (int iter = 0; iter < std::max(1, sim_case.iteration_num); ++iter) {
    (void)iter;
    for (const auto& batch : sim_case.batches) {
      for (int channel = 0; channel < kChannels; ++channel) {
        for (const Wide513& beat : batch[static_cast<size_t>(channel)]) {
          matrix[static_cast<size_t>(channel)].push_back(beat);
        }
      }
    }
  }
  return matrix;
}

void clear_peek_ports(VCuperSpmvOnly_ChiselDataPath8& top) {
  Wide513 wide_zero{};
  Tagged129 tagged_zero{};

  top.PE_Param_in_peek_dout = 0;
  top.PE_Param_in_peek_empty_n = 0;
  drive_wide(top.Vector_X_Stream_in_peek_dout, wide_zero);
  top.Vector_X_Stream_in_peek_empty_n = 0;

#define CLEAR_MATRIX_PEEK(ID)                                      \
  drive_wide(top.Matrix_A_Stream_##ID##_peek_dout, wide_zero);     \
  top.Matrix_A_Stream_##ID##_peek_empty_n = 0
  CLEAR_MATRIX_PEEK(0);
  CLEAR_MATRIX_PEEK(1);
  CLEAR_MATRIX_PEEK(2);
  CLEAR_MATRIX_PEEK(3);
  CLEAR_MATRIX_PEEK(4);
  CLEAR_MATRIX_PEEK(5);
  CLEAR_MATRIX_PEEK(6);
  CLEAR_MATRIX_PEEK(7);
#undef CLEAR_MATRIX_PEEK

#define CLEAR_TAGGED_PEEK(ID) \
  drive_wide(top.Vector_Y_Tagged_Stream_##ID##_peek, tagged_zero)
  CLEAR_TAGGED_PEEK(0);
  CLEAR_TAGGED_PEEK(1);
  CLEAR_TAGGED_PEEK(2);
  CLEAR_TAGGED_PEEK(3);
  CLEAR_TAGGED_PEEK(4);
  CLEAR_TAGGED_PEEK(5);
  CLEAR_TAGGED_PEEK(6);
  CLEAR_TAGGED_PEEK(7);
#undef CLEAR_TAGGED_PEEK
}

void drive_stream_inputs(VCuperSpmvOnly_ChiselDataPath8& top,
                         const std::deque<uint64_t>& pe,
                         const std::deque<Wide513>& x,
                         const std::array<std::deque<Wide513>, kChannels>& matrix) {
  top.PE_Param_in_s_dout = pe.empty() ? 0 : pe.front();
  top.PE_Param_in_s_empty_n = pe.empty() ? 0 : 1;

  drive_wide(top.Vector_X_Stream_in_s_dout, x.empty() ? zero_wide() : x.front());
  top.Vector_X_Stream_in_s_empty_n = x.empty() ? 0 : 1;

#define DRIVE_MATRIX(ID)                                                       \
  drive_wide(top.Matrix_A_Stream_##ID##_s_dout,                               \
             matrix[ID].empty() ? zero_wide() : matrix[ID].front());          \
  top.Matrix_A_Stream_##ID##_s_empty_n = matrix[ID].empty() ? 0 : 1
  DRIVE_MATRIX(0);
  DRIVE_MATRIX(1);
  DRIVE_MATRIX(2);
  DRIVE_MATRIX(3);
  DRIVE_MATRIX(4);
  DRIVE_MATRIX(5);
  DRIVE_MATRIX(6);
  DRIVE_MATRIX(7);
#undef DRIVE_MATRIX
}

void drive_output_ready(VCuperSpmvOnly_ChiselDataPath8& top, uint64_t cycle) {
  const bool ready = (cycle % 17) != 9 && (cycle % 23) != 5;
  top.Vector_Y_Tagged_Stream_0_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_1_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_2_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_3_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_4_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_5_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_6_s_full_n = ready ? 1 : 0;
  top.Vector_Y_Tagged_Stream_7_s_full_n = ready ? 1 : 0;
}

std::array<bool, kChannels> matrix_reads(const VCuperSpmvOnly_ChiselDataPath8& top) {
  return {static_cast<bool>(top.Matrix_A_Stream_0_s_read),
          static_cast<bool>(top.Matrix_A_Stream_1_s_read),
          static_cast<bool>(top.Matrix_A_Stream_2_s_read),
          static_cast<bool>(top.Matrix_A_Stream_3_s_read),
          static_cast<bool>(top.Matrix_A_Stream_4_s_read),
          static_cast<bool>(top.Matrix_A_Stream_5_s_read),
          static_cast<bool>(top.Matrix_A_Stream_6_s_read),
          static_cast<bool>(top.Matrix_A_Stream_7_s_read)};
}

bool tagged_write(const VCuperSpmvOnly_ChiselDataPath8& top, int owner) {
  switch (owner) {
    case 0: return top.Vector_Y_Tagged_Stream_0_s_write;
    case 1: return top.Vector_Y_Tagged_Stream_1_s_write;
    case 2: return top.Vector_Y_Tagged_Stream_2_s_write;
    case 3: return top.Vector_Y_Tagged_Stream_3_s_write;
    case 4: return top.Vector_Y_Tagged_Stream_4_s_write;
    case 5: return top.Vector_Y_Tagged_Stream_5_s_write;
    case 6: return top.Vector_Y_Tagged_Stream_6_s_write;
    default: return top.Vector_Y_Tagged_Stream_7_s_write;
  }
}

Tagged129 tagged_data(const VCuperSpmvOnly_ChiselDataPath8& top, int owner) {
  switch (owner) {
    case 0: return read_wide(top.Vector_Y_Tagged_Stream_0_s_din);
    case 1: return read_wide(top.Vector_Y_Tagged_Stream_1_s_din);
    case 2: return read_wide(top.Vector_Y_Tagged_Stream_2_s_din);
    case 3: return read_wide(top.Vector_Y_Tagged_Stream_3_s_din);
    case 4: return read_wide(top.Vector_Y_Tagged_Stream_4_s_din);
    case 5: return read_wide(top.Vector_Y_Tagged_Stream_5_s_din);
    case 6: return read_wide(top.Vector_Y_Tagged_Stream_6_s_din);
    default: return read_wide(top.Vector_Y_Tagged_Stream_7_s_din);
  }
}

void tick_low(VCuperSpmvOnly_ChiselDataPath8& top, VerilatedContext& context) {
  top.ap_clk = 0;
  top.eval();
  context.timeInc(5);
}

void tick_high(VCuperSpmvOnly_ChiselDataPath8& top, VerilatedContext& context) {
  top.ap_clk = 1;
  top.eval();
  context.timeInc(5);
}

bool same_float_bits(uint32_t got_bits, float expected) {
  return got_bits == float_bits(expected);
}

bool run_case(const SimCase& sim_case, CaseResult& result) {
  VerilatedContext context;
  VCuperSpmvOnly_ChiselDataPath8 top;

  std::deque<uint64_t> pe = pe_stream_for_case(sim_case);
  std::vector<Wide513> x_vec = x_packets_for_case(sim_case);
  std::deque<Wide513> x(x_vec.begin(), x_vec.end());
  std::array<std::deque<Wide513>, kChannels> matrix =
      matrix_streams_for_case(sim_case);
  const auto expected = expected_outputs(sim_case);

  const int expected_tagged = num_packets(sim_case.rows) * kChannels *
                              std::max(1, sim_case.iteration_num);
  std::vector<std::array<bool, kChannels>> seen(expected.size());
  for (auto& packet : seen) {
    packet.fill(false);
  }

  bool done_seen = false;
  int tagged_count = 0;
  for (uint64_t cycle = 0; cycle < 200000; ++cycle) {
    const bool reset = cycle < 5;
    top.ap_rst_n = reset ? 0 : 1;
    top.ap_start = (cycle == 5) ? 1 : 0;
    top.Iteration_num = static_cast<uint32_t>(sim_case.iteration_num);
    top.Row_num = static_cast<uint32_t>(sim_case.rows);
    top.Batch_num = static_cast<uint32_t>(sim_case.batches.size());
    top.Matrix_len = 0;
    top.Column_num = kColumns;

    clear_peek_ports(top);
    drive_stream_inputs(top, pe, x, matrix);
    drive_output_ready(top, cycle);

    tick_low(top, context);

    const bool pe_read = top.PE_Param_in_s_read;
    const bool x_read = top.Vector_X_Stream_in_s_read;
    const auto matrix_read = matrix_reads(top);
    std::array<bool, kChannels> write{};
    std::array<Tagged129, kChannels> write_data{};
    for (int owner = 0; owner < kChannels; ++owner) {
      write[static_cast<size_t>(owner)] = tagged_write(top, owner);
      if (write[static_cast<size_t>(owner)]) {
        write_data[static_cast<size_t>(owner)] = tagged_data(top, owner);
      }
    }

    tick_high(top, context);

    if (pe_read) {
      if (pe.empty()) {
        std::cerr << sim_case.name << ": PE stream underflow at cycle "
                  << cycle << "\n";
        return false;
      }
      pe.pop_front();
    }
    if (x_read) {
      if (x.empty()) {
        std::cerr << sim_case.name << ": X stream underflow at cycle "
                  << cycle << "\n";
        return false;
      }
      x.pop_front();
    }
    for (int source = 0; source < kChannels; ++source) {
      if (matrix_read[static_cast<size_t>(source)]) {
        if (matrix[static_cast<size_t>(source)].empty()) {
          std::cerr << sim_case.name << ": Matrix stream " << source
                    << " underflow at cycle " << cycle << "\n";
          return false;
        }
        matrix[static_cast<size_t>(source)].pop_front();
      }
    }

    for (int owner = 0; owner < kChannels; ++owner) {
      if (!write[static_cast<size_t>(owner)]) {
        continue;
      }
      const auto& word = write_data[static_cast<size_t>(owner)];
      const uint32_t packet_idx = word[0];
      const uint32_t pair_lane = word[1];
      const uint32_t ping = word[2];
      const uint32_t pong = word[3];
      const uint32_t pad = word[4] & 1U;

      if (pad != 0 || packet_idx >= expected.size() || pair_lane >= kChannels) {
        std::cerr << sim_case.name << ": bad tagged word at cycle " << cycle
                  << ": pad=" << pad
                  << " packet=" << packet_idx
                  << " pair=" << pair_lane << "\n";
        return false;
      }
      if (static_cast<int>(packet_idx % kChannels) != owner) {
        std::cerr << sim_case.name << ": owner stream mismatch at cycle "
                  << cycle << ": stream=" << owner
                  << " packet=" << packet_idx << "\n";
        return false;
      }
      if (seen[packet_idx][pair_lane]) {
        std::cerr << sim_case.name << ": duplicate tagged output packet="
                  << packet_idx << " pair=" << pair_lane << "\n";
        return false;
      }
      seen[packet_idx][pair_lane] = true;

      const auto& exp = expected[packet_idx][pair_lane];
      if (!same_float_bits(ping, exp[0]) || !same_float_bits(pong, exp[1])) {
        std::cerr << std::hex << std::setfill('0')
                  << sim_case.name << ": value mismatch packet=" << std::dec
                  << packet_idx << " pair=" << pair_lane
                  << " got ping=0x" << std::hex << std::setw(8) << ping
                  << " pong=0x" << std::setw(8) << pong
                  << " expected ping=0x" << std::setw(8) << float_bits(exp[0])
                  << " pong=0x" << std::setw(8) << float_bits(exp[1])
                  << std::dec << " got_float=(" << bits_float(ping)
                  << ", " << bits_float(pong) << ")"
                  << " expected_float=(" << exp[0] << ", " << exp[1]
                  << ")\n";
        return false;
      }
      ++tagged_count;
    }

    if (top.ap_done) {
      done_seen = true;
      result.cycle_count = cycle;
      break;
    }
  }

  if (!done_seen) {
    std::cerr << sim_case.name << ": timed out before ap_done\n";
    return false;
  }
  if (tagged_count != expected_tagged) {
    std::cerr << sim_case.name << ": expected " << expected_tagged
              << " tagged outputs, got " << tagged_count << "\n";
    return false;
  }
  for (int packet = 0; packet < static_cast<int>(seen.size()); ++packet) {
    for (int pair = 0; pair < kChannels; ++pair) {
      if (!seen[static_cast<size_t>(packet)][static_cast<size_t>(pair)]) {
        std::cerr << sim_case.name << ": missing tagged output packet="
                  << packet << " pair=" << pair << "\n";
        return false;
      }
    }
  }
  if (!pe.empty() || !x.empty()) {
    std::cerr << sim_case.name << ": input queues not fully consumed: pe="
              << pe.size() << " x=" << x.size() << "\n";
    return false;
  }
  for (int source = 0; source < kChannels; ++source) {
    if (!matrix[static_cast<size_t>(source)].empty()) {
      std::cerr << sim_case.name << ": matrix queue " << source
                << " not fully consumed\n";
      return false;
    }
  }

  result.tagged_count = tagged_count;
  return true;
}

SimCase make_basic_case() {
  SimCase sim_case;
  sim_case.name = "basic-two-beat";
  sim_case.rows = 128;
  sim_case.batches.resize(1);
  for (int source = 0; source < kChannels; ++source) {
    auto ping = padded_slots();
    auto pong = padded_slots();
    for (int owner = 0; owner < kChannels; ++owner) {
      set_slot(ping, owner, owner, 0, owner, static_cast<float>(source + 1));
      set_slot(pong, owner, owner, 1, owner + 8,
               static_cast<float>(source + 9));
    }
    sim_case.batches[0][static_cast<size_t>(source)].push_back(pack_matrix_beat(ping));
    sim_case.batches[0][static_cast<size_t>(source)].push_back(pack_matrix_beat(pong));
  }
  return sim_case;
}

SimCase make_raw_reuse_case() {
  SimCase sim_case;
  sim_case.name = "raw-reuse-padding";
  sim_case.rows = 128;
  sim_case.batches.resize(1);

  auto beat0 = padded_slots();
  set_slot(beat0, 0, 0, 0, 3, 2.0f);
  set_slot(beat0, 1, 1, 0, 4, 3.0f);
  set_slot(beat0, 2, 2, 1, 5, 4.0f);
  sim_case.batches[0][0].push_back(pack_matrix_beat(beat0));

  auto beat1 = padded_slots();
  set_slot(beat1, 0, 0, 0, 3, 5.0f);
  set_reuse_slot(beat1, 1, 1, 0);
  set_slot(beat1, 2, 2, 1, 5, 6.0f);
  sim_case.batches[0][0].push_back(pack_matrix_beat(beat1));

  auto beat2 = padded_slots();
  set_slot(beat2, 0, 0, 1, 6, 7.0f);
  set_reuse_slot(beat2, 1, 1, 1);
  sim_case.batches[0][3].push_back(pack_matrix_beat(beat2));

  return sim_case;
}

SimCase make_multi_group_case() {
  SimCase sim_case;
  sim_case.name = "multi-group-empty-source";
  sim_case.rows = 272;
  sim_case.batches.resize(1);

  auto b0s0 = padded_slots();
  set_slot(b0s0, 0, 0, 0, 0, 1.0f);
  set_slot(b0s0, 7, 15, 1, 15, 2.0f);
  sim_case.batches[0][0].push_back(pack_matrix_beat(b0s0));

  auto b0s5 = padded_slots();
  set_slot(b0s5, 0, 8, 0, 8, 3.0f);
  set_slot(b0s5, 1, 9, 1, 9, 4.0f);
  sim_case.batches[0][5].push_back(pack_matrix_beat(b0s5));

  auto b0s2 = padded_slots();
  set_slot(b0s2, 0, 16, 0, 16, 5.0f);
  set_slot(b0s2, 1, 16, 1, 17, 6.0f);
  sim_case.batches[0][2].push_back(pack_matrix_beat(b0s2));

  return sim_case;
}

SimCase make_all_padding_case() {
  SimCase sim_case;
  sim_case.name = "all-padding";
  sim_case.rows = 32;
  sim_case.batches.resize(1);
  sim_case.batches[0][4].push_back(pack_matrix_beat(padded_slots()));
  return sim_case;
}

}  // namespace

extern "C" int cuper_verilator_fmul32(int a, int b) {
  return static_cast<int>(
      float_result_bits(bits_float(static_cast<uint32_t>(a)) *
                        bits_float(static_cast<uint32_t>(b))));
}

extern "C" int cuper_verilator_fadd32(int a, int b) {
  return static_cast<int>(
      float_result_bits(bits_float(static_cast<uint32_t>(a)) +
                        bits_float(static_cast<uint32_t>(b))));
}

int main(int argc, char** argv) {
  VerilatedContext context;
  context.commandArgs(argc, argv);

  const std::array<SimCase, 4> cases = {
      make_basic_case(),
      make_raw_reuse_case(),
      make_multi_group_case(),
      make_all_padding_case(),
  };

  int total_tagged = 0;
  for (const SimCase& sim_case : cases) {
    CaseResult result;
    if (!run_case(sim_case, result)) {
      return 1;
    }
    total_tagged += result.tagged_count;
    std::cout << "PASS " << sim_case.name << ": " << result.tagged_count
              << " tagged outputs in " << result.cycle_count << " cycles\n";
  }

  std::cout << "CuperSpmvOnly_ChiselDataPath8 smoke PASS: "
            << total_tagged << " tagged outputs across " << cases.size()
            << " cases\n";
  return 0;
}
