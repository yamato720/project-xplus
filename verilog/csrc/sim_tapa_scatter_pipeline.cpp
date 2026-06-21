#include "VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.h"

#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr int kPairs = 8;
constexpr int kScalars = 16;

uint32_t fbits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::array<uint32_t, 5> pack_tagged_pair(uint32_t packet_idx,
                                         uint32_t pair_lane,
                                         uint32_t value0,
                                         uint32_t value1) {
  // Verilated VlWide word order is little-endian 32-bit chunks.
  // RTL layout: {pad[128], value1[127:96], value0[95:64],
  //              pair_lane[63:32], packet_idx[31:0]}.
  return {packet_idx, pair_lane, value0, value1, 0};
}

void drive_word(VlWide<5>& port, const std::array<uint32_t, 5>& word) {
  for (int i = 0; i < 5; ++i) {
    port[i] = word[static_cast<size_t>(i)];
  }
}

uint32_t expected_value(int idx) {
  if ((idx & 1) == 0) {
    return fbits(10.0f + static_cast<float>(idx >> 1));
  }
  return fbits(100.0f + static_cast<float>(idx >> 1));
}

void eval_half(VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel& top,
               VerilatedContext& context,
               int clk) {
  top.ap_clk = clk;
  top.eval();
  context.timeInc(5);
}

void clear_unused_streams(VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel& top) {
  top.Vector_Y_Tagged_Stream_1_empty_n = 0;
  top.Vector_Y_Tagged_Stream_2_empty_n = 0;
  top.Vector_Y_Tagged_Stream_3_empty_n = 0;
  top.Vector_Y_Tagged_Stream_4_empty_n = 0;
  top.Vector_Y_Tagged_Stream_5_empty_n = 0;
  top.Vector_Y_Tagged_Stream_6_empty_n = 0;
  top.Vector_Y_Tagged_Stream_7_empty_n = 0;
  top.Vector_Y_Tagged_Stream_8_empty_n = 0;
  top.Vector_Y_Tagged_Stream_9_empty_n = 0;
  top.Vector_Y_Tagged_Stream_10_empty_n = 0;
  top.Vector_Y_Tagged_Stream_11_empty_n = 0;
  top.Vector_Y_Tagged_Stream_12_empty_n = 0;
  top.Vector_Y_Tagged_Stream_13_empty_n = 0;
  top.Vector_Y_Tagged_Stream_14_empty_n = 0;
  top.Vector_Y_Tagged_Stream_15_empty_n = 0;
}

}  // namespace

int main(int argc, char** argv) {
  VerilatedContext context;
  context.commandArgs(argc, argv);

  VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel top;
  std::vector<std::array<uint32_t, 5>> input;
  input.reserve(kPairs);
  for (int i = 0; i < kPairs; ++i) {
    input.push_back(pack_tagged_pair(0,
                                     static_cast<uint32_t>(i),
                                     fbits(10.0f + static_cast<float>(i)),
                                     fbits(100.0f + static_cast<float>(i))));
  }

  std::array<uint32_t, kScalars> y{};
  std::array<bool, kScalars> seen{};
  size_t input_idx = 0;
  int pending_responses = 0;
  int writes = 0;

  for (uint64_t cycle = 0; cycle < 1000; ++cycle) {
    const bool rst = cycle < 5;
    const bool response_available = !rst && pending_responses > 0;
    top.ap_rst = rst ? 1 : 0;
    top.ap_start = (cycle == 5) ? 1 : 0;
    top.scalar_writes_total = kScalars;
    top.tagged_pairs_total = kPairs;
    top.Y_out_write_addr_s_full_n = 1;
    top.Y_out_write_data_s_full_n = 1;
    top.Y_out_write_addr_offset_load = 0;
    top.Y_out_write_resp_s_dout = 0;
    top.Y_out_write_resp_s_empty_n = response_available ? 1 : 0;
    clear_unused_streams(top);

    if (!rst && input_idx < input.size()) {
      drive_word(top.Vector_Y_Tagged_Stream_0_dout, input[input_idx]);
      top.Vector_Y_Tagged_Stream_0_empty_n = 1;
    } else {
      std::array<uint32_t, 5> zero{};
      drive_word(top.Vector_Y_Tagged_Stream_0_dout, zero);
      top.Vector_Y_Tagged_Stream_0_empty_n = 0;
    }

    eval_half(top, context, 0);
    eval_half(top, context, 1);

    const bool read_fire = !rst && top.Vector_Y_Tagged_Stream_0_read &&
                           top.Vector_Y_Tagged_Stream_0_empty_n;
    if (!rst && top.Y_out_write_addr_s_write) {
      if (!top.Y_out_write_data_s_write) {
        std::cerr << "FAIL: addr write without data write\n";
        return 1;
      }
      const uint64_t addr = top.Y_out_write_addr_s_din >> 2;
      if (addr >= y.size()) {
        std::cerr << "FAIL: bad addr=" << top.Y_out_write_addr_s_din << "\n";
        return 1;
      }
      y[addr] = static_cast<uint32_t>(top.Y_out_write_data_s_din);
      seen[addr] = true;
      ++writes;
      ++pending_responses;
    }
    if (!rst && top.Y_out_write_resp_s_read) {
      if (!response_available) {
        std::cerr << "FAIL: response read on empty stream\n";
        return 1;
      }
      --pending_responses;
    }

    if (read_fire) {
      ++input_idx;
    }
    if (!rst && top.ap_done) {
      if (input_idx != input.size()) {
        std::cerr << "FAIL: input_idx=" << input_idx
                  << " expect=" << input.size() << "\n";
        return 1;
      }
      if (writes != kScalars) {
        std::cerr << "FAIL: writes=" << writes << " expect=" << kScalars << "\n";
        return 1;
      }
      int errors = 0;
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
      std::cout << "PASS: scatter cpp harness cycles=" << cycle
                << " writes=" << writes << "\n";
      top.final();
      return 0;
    }
  }

  std::cerr << "FAIL: timeout input_idx=" << input_idx
            << " writes=" << writes
            << " pending_responses=" << pending_responses << "\n";
  return 1;
}
