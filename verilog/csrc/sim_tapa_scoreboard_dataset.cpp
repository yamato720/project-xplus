#include "VCuperSpmvOnly_RtlIssueScoreboard8.h"

#include "verilated.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kHbmChannels = 16;
constexpr int kPeNum = 8;
constexpr int kOwnerLaneStreams = kHbmChannels * kPeNum;
constexpr int kGroupSize = kHbmChannels / 8;
constexpr int kSliceSize = kHbmChannels * 4;
constexpr int kBatchSize = 8192 / kSliceSize;
constexpr int kSliceWidth = kSliceSize * kBatchSize;
constexpr int kAddrWidth = 13;
constexpr int kTaggedWidth = 130;
constexpr int kTaggedPadBit = 129;
constexpr int kDefaultTimeoutCycles = 2000000;

struct Csr {
  int rows = 0;
  int cols = 0;
  std::vector<int> row_ptr;
  std::vector<int> col_idx;
  std::vector<float> values;
};

struct SpElem {
  int col = 0;
  int row = 0;
  float value = 0.0f;
};

struct TaggedScalar {
  bool done = false;
  uint32_t packet_idx = 0;
  uint32_t pair_lane = 0;
  uint32_t scalar_lane = 0;
  float value = 0.0f;
  int source = 0;
  int slot_lane = 0;
  int batch = 0;
  int local_col = 0;

  uint32_t addr() const { return packet_idx / kHbmChannels; }
  bool is_pong() const { return scalar_lane != 0; }
};

struct SplitBeat {
  int source = 0;
  int batch = 0;
  bool done_beat = false;
  std::array<bool, kPeNum> valid{};
  std::array<TaggedScalar, kPeNum> token{};
};

using BatchedLaneLists =
    std::vector<std::array<std::vector<SpElem>, kOwnerLaneStreams>>;
using OwnerLaneQueues = std::array<std::deque<TaggedScalar>, kPeNum>;
using OwnerFifos = std::array<OwnerLaneQueues, kHbmChannels>;
using SourceBeats = std::array<std::vector<SplitBeat>, kHbmChannels>;
using Scoreboards =
    std::array<std::unique_ptr<VCuperSpmvOnly_RtlIssueScoreboard8>,
               kHbmChannels>;

template <typename T>
std::vector<T> read_array(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open " + path);
  }

  std::vector<T> out;
  T value{};
  while (input >> value) {
    out.push_back(value);
  }
  return out;
}

std::string join_path(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir.back() == '/') {
    return dir + name;
  }
  return dir + "/" + name;
}

int parse_meta_int(const std::string& path, const std::string& key,
                   int fallback) {
  std::ifstream input(path);
  if (!input) {
    return fallback;
  }

  std::string line;
  const std::string prefix = key + "=";
  while (std::getline(input, line)) {
    if (line.rfind(prefix, 0) == 0) {
      return std::stoi(line.substr(prefix.size()));
    }
  }
  return fallback;
}

Csr read_csr(const std::string& dir) {
  Csr csr;
  csr.row_ptr = read_array<int>(join_path(dir, "row_ptr.txt"));
  csr.col_idx = read_array<int>(join_path(dir, "col_idx.txt"));
  const auto values_double = read_array<double>(join_path(dir, "values.txt"));
  csr.values.reserve(values_double.size());
  for (double value : values_double) {
    csr.values.push_back(static_cast<float>(value));
  }

  if (csr.row_ptr.size() < 2 || csr.row_ptr.front() != 0) {
    throw std::runtime_error("bad CSR row_ptr");
  }
  csr.rows = parse_meta_int(join_path(dir, "meta.txt"), "m",
                            static_cast<int>(csr.row_ptr.size()) - 1);
  csr.cols = parse_meta_int(join_path(dir, "meta.txt"), "n", csr.rows);
  if (csr.rows + 1 != static_cast<int>(csr.row_ptr.size())) {
    throw std::runtime_error("CSR row count does not match meta.txt");
  }

  const int nnz = csr.row_ptr.back();
  if (nnz < 0 || static_cast<size_t>(nnz) != csr.col_idx.size() ||
      static_cast<size_t>(nnz) != csr.values.size()) {
    throw std::runtime_error("CSR nnz mismatch");
  }
  return csr;
}

int map_row_to_pe(int row) {
  const int packet_id = row / 2;
  const int checker_id = packet_id % 8;
  const int acc_offset = (packet_id / 8) % kGroupSize;
  const int pe_in_acc = (packet_id / kHbmChannels) % kPeNum;
  return (checker_id * kGroupSize + acc_offset) * kPeNum + pe_in_acc;
}

uint32_t tagged_packet_index_from_slot(int source_core,
                                       int local_row_group,
                                       int slot_lane) {
  const int acc_offset = source_core % kGroupSize;
  return static_cast<uint32_t>(
      local_row_group * kHbmChannels + slot_lane * kGroupSize + acc_offset);
}

uint32_t owner_from_packet(uint32_t packet_idx) {
  return packet_idx % kHbmChannels;
}

BatchedLaneLists make_batched_lane_lists(const Csr& csr) {
  const int batch_num = (csr.cols + kSliceWidth - 1) / kSliceWidth;
  BatchedLaneLists batches(static_cast<size_t>(batch_num));

  for (int row = 0; row < csr.rows; ++row) {
    for (int off = csr.row_ptr[static_cast<size_t>(row)];
         off < csr.row_ptr[static_cast<size_t>(row + 1)]; ++off) {
      const int col = csr.col_idx[static_cast<size_t>(off)];
      if (col < 0 || col >= csr.cols) {
        throw std::runtime_error("CSR column index outside matrix shape");
      }
      const int batch = col / kSliceWidth;
      const int base_col = batch * kSliceWidth;
      const int pe_idx = map_row_to_pe(row);
      const int encoded_row =
          (row / (2 * kOwnerLaneStreams)) * 2 + (row & 1);
      batches[static_cast<size_t>(batch)][static_cast<size_t>(pe_idx)]
          .push_back(SpElem{col - base_col, encoded_row,
                            csr.values[static_cast<size_t>(off)]});
    }
  }

  for (auto& batch : batches) {
    for (auto& lane_list : batch) {
      std::sort(lane_list.begin(), lane_list.end(),
                [](const SpElem& lhs, const SpElem& rhs) {
                  if (lhs.col != rhs.col) {
                    return lhs.col < rhs.col;
                  }
                  return lhs.row < rhs.row;
                });
    }
  }

  return batches;
}

OwnerLaneQueues make_owner_lane_streams(const BatchedLaneLists& batches,
                                        int owner) {
  if (owner < 0 || owner >= kHbmChannels) {
    throw std::runtime_error("owner must be in [0, 15]");
  }

  std::array<std::deque<TaggedScalar>, kPeNum> owner_lanes;
  const int batch_num = static_cast<int>(batches.size());
  const int owner_slot_lane = owner / kGroupSize;
  const int owner_source_offset = owner % kGroupSize;

  for (int pair_lane = 0; pair_lane < kPeNum; ++pair_lane) {
    const int source = pair_lane * kGroupSize + owner_source_offset;
    const int pe_idx = source * kPeNum + owner_slot_lane;

    for (int batch = 0; batch < batch_num; ++batch) {
      const auto& lane_list =
          batches[static_cast<size_t>(batch)][static_cast<size_t>(pe_idx)];
      for (const SpElem& sp : lane_list) {
        const int local_row_group = sp.row >> 1;
        const int scalar_lane = sp.row & 1;
        TaggedScalar tagged;
        tagged.done = false;
        tagged.packet_idx = tagged_packet_index_from_slot(
            source, local_row_group, owner_slot_lane);
        tagged.pair_lane = static_cast<uint32_t>(pair_lane);
        tagged.scalar_lane = static_cast<uint32_t>(scalar_lane);
        tagged.value = sp.value;
        tagged.source = source;
        tagged.slot_lane = owner_slot_lane;
        tagged.batch = batch;
        tagged.local_col = sp.col;
        if (owner_from_packet(tagged.packet_idx) !=
            static_cast<uint32_t>(owner)) {
          throw std::runtime_error("internal owner mapping mismatch");
        }
        owner_lanes[static_cast<size_t>(pair_lane)].push_back(tagged);
      }
    }

    TaggedScalar done;
    done.done = true;
    done.packet_idx =
        tagged_packet_index_from_slot(source, 0, owner_slot_lane);
    done.pair_lane = static_cast<uint32_t>(pair_lane);
    done.scalar_lane = 0;
    done.source = source;
    done.slot_lane = owner_slot_lane;
    done.batch = batch_num;
    owner_lanes[static_cast<size_t>(pair_lane)].push_back(done);
  }

  return owner_lanes;
}

SourceBeats make_source_beats(const BatchedLaneLists& batches) {
  SourceBeats sources;

  for (int source = 0; source < kHbmChannels; ++source) {
    for (int batch = 0; batch < static_cast<int>(batches.size()); ++batch) {
      size_t channel_batch_len = 0;
      for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
        const int pe_idx = source * kPeNum + slot_lane;
        channel_batch_len =
            std::max(channel_batch_len,
                     batches[static_cast<size_t>(batch)]
                            [static_cast<size_t>(pe_idx)]
                                .size());
      }

      for (size_t beat_idx = 0; beat_idx < channel_batch_len; ++beat_idx) {
        SplitBeat beat;
        beat.source = source;
        beat.batch = batch;
        beat.done_beat = false;
        beat.valid.fill(false);

        for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
          const int pe_idx = source * kPeNum + slot_lane;
          const auto& lane_list =
              batches[static_cast<size_t>(batch)]
                     [static_cast<size_t>(pe_idx)];
          if (beat_idx >= lane_list.size()) {
            continue;
          }

          const SpElem& sp = lane_list[beat_idx];
          const int local_row_group = sp.row >> 1;
          const int scalar_lane = sp.row & 1;
          TaggedScalar tagged;
          tagged.done = false;
          tagged.packet_idx = tagged_packet_index_from_slot(
              source, local_row_group, slot_lane);
          tagged.pair_lane = static_cast<uint32_t>(source / kGroupSize);
          tagged.scalar_lane = static_cast<uint32_t>(scalar_lane);
          tagged.value = sp.value;
          tagged.source = source;
          tagged.slot_lane = slot_lane;
          tagged.batch = batch;
          tagged.local_col = sp.col;

          const uint32_t expected_owner =
              static_cast<uint32_t>(slot_lane * kGroupSize +
                                    (source % kGroupSize));
          if (owner_from_packet(tagged.packet_idx) != expected_owner) {
            throw std::runtime_error("internal source owner mapping mismatch");
          }

          beat.valid[static_cast<size_t>(slot_lane)] = true;
          beat.token[static_cast<size_t>(slot_lane)] = tagged;
        }

        sources[static_cast<size_t>(source)].push_back(beat);
      }
    }

    SplitBeat done_beat;
    done_beat.source = source;
    done_beat.batch = static_cast<int>(batches.size());
    done_beat.done_beat = true;
    done_beat.valid.fill(true);
    for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
      TaggedScalar done;
      done.done = true;
      done.packet_idx = tagged_packet_index_from_slot(source, 0, slot_lane);
      done.pair_lane = static_cast<uint32_t>(source / kGroupSize);
      done.scalar_lane = 0;
      done.source = source;
      done.slot_lane = slot_lane;
      done.batch = static_cast<int>(batches.size());
      done_beat.token[static_cast<size_t>(slot_lane)] = done;
    }
    sources[static_cast<size_t>(source)].push_back(done_beat);
  }

  return sources;
}

bool queues_empty(const std::array<std::deque<TaggedScalar>, kPeNum>& lanes) {
  for (const auto& lane : lanes) {
    if (!lane.empty()) {
      return false;
    }
  }
  return true;
}

template <size_t Words>
void clear_wide(VlWide<Words>& word) {
  for (size_t i = 0; i < Words; ++i) {
    word[i] = 0;
  }
}

template <size_t Words>
void set_bits(VlWide<Words>& word, int lo, int width, uint32_t value) {
  for (int bit = 0; bit < width; ++bit) {
    if ((value >> bit) & 1U) {
      const int pos = lo + bit;
      word[pos / 32] |= 1U << static_cast<unsigned>(pos % 32);
    }
  }
}

template <size_t Words>
bool get_bit(const VlWide<Words>& word, int bit) {
  return ((word[bit / 32] >> static_cast<unsigned>(bit % 32)) & 1U) != 0;
}

uint32_t float_to_bits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void pack_tagged_payload(VlWide<33>& payload, int lane,
                         const TaggedScalar& tagged) {
  const int base = lane * kTaggedWidth;
  if (tagged.done) {
    set_bits(payload, base, 1, 1);
  }
  set_bits(payload, base + 1, 32, tagged.packet_idx);
  set_bits(payload, base + 33, 32, tagged.pair_lane);
  set_bits(payload, base + 65, 32, tagged.scalar_lane);
  set_bits(payload, base + 97, 32, float_to_bits(tagged.value));
}

uint8_t issue_real_mask(const VCuperSpmvOnly_RtlIssueScoreboard8& dut) {
  uint8_t mask = 0;
  for (int lane = 0; lane < kPeNum; ++lane) {
    if (!get_bit(dut.issue_payload, lane * kTaggedWidth + kTaggedPadBit)) {
      mask |= static_cast<uint8_t>(1U << lane);
    }
  }
  return mask;
}

void drive_heads(VCuperSpmvOnly_RtlIssueScoreboard8& dut,
                 const std::array<std::deque<TaggedScalar>, kPeNum>& lanes) {
  dut.head_valid = 0;
  dut.head_is_pong = 0;
  dut.head_done = 0;
  clear_wide(dut.head_addr);
  clear_wide(dut.head_payload);

  for (int lane = 0; lane < kPeNum; ++lane) {
    const auto& queue = lanes[static_cast<size_t>(lane)];
    if (queue.empty()) {
      continue;
    }

    const TaggedScalar& tagged = queue.front();
    dut.head_valid |= static_cast<uint8_t>(1U << lane);
    if (tagged.is_pong()) {
      dut.head_is_pong |= static_cast<uint8_t>(1U << lane);
    }
    if (tagged.done) {
      dut.head_done |= static_cast<uint8_t>(1U << lane);
    }
    set_bits(dut.head_addr, lane * kAddrWidth, kAddrWidth, tagged.addr());
    pack_tagged_payload(dut.head_payload, lane, tagged);
  }
}

void eval_clock(VCuperSpmvOnly_RtlIssueScoreboard8& dut,
                VerilatedContext& context, int clk) {
  dut.clk = clk;
  dut.eval();
  context.timeInc(1);
}

struct Options {
  std::string matrix = "../data/suitesparse/Schmid/csr/thermal2_n1024";
  int owner = 0;
  int trace = 32;
  int fifo_depth = 0;
  int timeout_cycles = kDefaultTimeoutCycles;
  std::string csv_path;
};

Options parse_options(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--matrix" && i + 1 < argc) {
      opt.matrix = argv[++i];
    } else if (arg == "--owner" && i + 1 < argc) {
      opt.owner = std::stoi(argv[++i]);
    } else if (arg == "--trace" && i + 1 < argc) {
      opt.trace = std::stoi(argv[++i]);
    } else if (arg == "--fifo-depth" && i + 1 < argc) {
      opt.fifo_depth = std::stoi(argv[++i]);
    } else if (arg == "--timeout-cycles" && i + 1 < argc) {
      opt.timeout_cycles = std::stoi(argv[++i]);
    } else if (arg == "--csv" && i + 1 < argc) {
      opt.csv_path = argv[++i];
    } else {
      std::cerr << "usage: " << argv[0]
                << " [--matrix CSR_DIR] [--owner 0..15] [--trace N]"
                << " [--fifo-depth N] [--timeout-cycles N] [--csv PATH]\n";
      std::exit(2);
    }
  }
  return opt;
}

void eval_all(Scoreboards& duts, VerilatedContext& context, int clk) {
  for (auto& dut : duts) {
    dut->clk = clk;
    dut->eval();
  }
  context.timeInc(1);
}

bool all_fifos_empty(const OwnerFifos& fifos) {
  for (const auto& owner : fifos) {
    if (!queues_empty(owner)) {
      return false;
    }
  }
  return true;
}

bool all_sources_done(const SourceBeats& sources,
                      const std::array<size_t, kHbmChannels>& source_pos) {
  for (int source = 0; source < kHbmChannels; ++source) {
    if (source_pos[static_cast<size_t>(source)] <
        sources[static_cast<size_t>(source)].size()) {
      return false;
    }
  }
  return true;
}

bool can_enqueue_beat(const SplitBeat& beat, const OwnerFifos& fifos,
                      int fifo_depth, int* blocked_owner,
                      int* blocked_lane) {
  for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
    if (!beat.valid[static_cast<size_t>(slot_lane)]) {
      continue;
    }
    const TaggedScalar& tagged = beat.token[static_cast<size_t>(slot_lane)];
    const int owner = static_cast<int>(owner_from_packet(tagged.packet_idx));
    const int lane = static_cast<int>(tagged.pair_lane);
    if (static_cast<int>(
            fifos[static_cast<size_t>(owner)][static_cast<size_t>(lane)]
                .size()) >= fifo_depth) {
      if (blocked_owner) {
        *blocked_owner = owner;
      }
      if (blocked_lane) {
        *blocked_lane = lane;
      }
      return false;
    }
  }
  return true;
}

void enqueue_beat(const SplitBeat& beat, OwnerFifos& fifos,
                  std::array<std::array<size_t, kPeNum>, kHbmChannels>&
                      max_fifo_occupancy) {
  for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
    if (!beat.valid[static_cast<size_t>(slot_lane)]) {
      continue;
    }
    const TaggedScalar& tagged = beat.token[static_cast<size_t>(slot_lane)];
    const int owner = static_cast<int>(owner_from_packet(tagged.packet_idx));
    const int lane = static_cast<int>(tagged.pair_lane);
    auto& fifo = fifos[static_cast<size_t>(owner)][static_cast<size_t>(lane)];
    fifo.push_back(tagged);
    max_fifo_occupancy[static_cast<size_t>(owner)]
                      [static_cast<size_t>(lane)] =
        std::max(max_fifo_occupancy[static_cast<size_t>(owner)]
                                   [static_cast<size_t>(lane)],
                 fifo.size());
  }
}

int run_direct_owner(const Options& opt, const Csr& csr,
                     OwnerLaneQueues lanes) {
  std::array<size_t, kPeNum> initial_lane_tokens{};
  size_t initial_real_tokens = 0;
  size_t initial_done_tokens = 0;
  for (int lane = 0; lane < kPeNum; ++lane) {
    initial_lane_tokens[static_cast<size_t>(lane)] =
        lanes[static_cast<size_t>(lane)].size();
    for (const TaggedScalar& tagged : lanes[static_cast<size_t>(lane)]) {
      if (tagged.done) {
        ++initial_done_tokens;
      } else {
        ++initial_real_tokens;
      }
    }
  }

  std::ofstream csv;
  if (!opt.csv_path.empty()) {
    csv.open(opt.csv_path);
    if (!csv) {
      std::cerr << "FAIL: failed to open csv " << opt.csv_path << "\n";
      return 1;
    }
    csv << "cycle,lane,done,packet_idx,addr,is_pong,pair_lane,scalar_lane,"
           "source,slot_lane,batch,local_col,value,hazard_mask,issue_mask\n";
  }

  VerilatedContext context;
  VCuperSpmvOnly_RtlIssueScoreboard8 dut;

  dut.issue_ready = 1;
  dut.rst = 1;
  drive_heads(dut, lanes);
  for (int i = 0; i < 3; ++i) {
    eval_clock(dut, context, 0);
    eval_clock(dut, context, 1);
  }
  dut.rst = 0;

  size_t cycles = 0;
  size_t issue_beats = 0;
  size_t padding_beats = 0;
  size_t issue_count = 0;
  size_t real_issue_count = 0;
  size_t done_issue_count = 0;
  size_t bypass_cycles = 0;
  size_t issue_complete_cycle = 0;
  std::array<size_t, kPeNum> issued_per_lane{};
  bool queues_drained = false;

  while (cycles < static_cast<size_t>(opt.timeout_cycles)) {
    drive_heads(dut, lanes);
    eval_clock(dut, context, 0);

    const bool any_valid = dut.head_valid != 0;
    const bool issue = dut.issue_valid && dut.issue_ready;
    const uint8_t hazard_mask = dut.lane_hazard;
    const uint8_t real_mask = issue ? issue_real_mask(dut) : 0;
    if (any_valid && (hazard_mask & dut.head_valid) != 0 && issue) {
      ++bypass_cycles;
    }

    if (issue) {
      ++issue_beats;
      if (real_mask == 0) {
        ++padding_beats;
      }
      for (int lane = 0; lane < kPeNum; ++lane) {
        if (((real_mask >> lane) & 1U) == 0) {
          continue;
        }

        const TaggedScalar& issued = lanes[static_cast<size_t>(lane)].front();
        ++issue_count;
        ++issued_per_lane[static_cast<size_t>(lane)];
        if (issued.done) {
          ++done_issue_count;
        } else {
          ++real_issue_count;
        }

        if (static_cast<int>(issue_count) <= opt.trace) {
          std::cout << "trace cycle=" << cycles
                  << " mask=0x" << std::hex << static_cast<int>(real_mask)
                  << std::dec
                  << " lane=" << lane
                  << " done=" << issued.done
                  << " packet=" << issued.packet_idx
                  << " addr=" << issued.addr()
                  << " pingpong=" << (issued.is_pong() ? "pong" : "ping")
                  << " pair=" << issued.pair_lane
                  << " scalar=" << issued.scalar_lane
                  << " source=" << issued.source
                  << " slot_lane=" << issued.slot_lane
                  << " batch=" << issued.batch
                  << " local_col=" << issued.local_col
                  << " value=" << issued.value
                  << " hazard=0x" << std::hex
                  << static_cast<int>(hazard_mask)
                  << std::dec << "\n";
        }

        if (csv) {
          csv << cycles << "," << lane << "," << issued.done << ","
            << issued.packet_idx << "," << issued.addr() << ","
            << issued.is_pong() << "," << issued.pair_lane << ","
            << issued.scalar_lane << "," << issued.source << ","
            << issued.slot_lane << "," << issued.batch << ","
            << issued.local_col << "," << std::setprecision(9) << issued.value
            << ",0x" << std::hex << static_cast<int>(hazard_mask)
            << ",0x" << static_cast<int>(real_mask) << std::dec << "\n";
        }
      }
    }

    eval_clock(dut, context, 1);
    if (issue) {
      for (int lane = 0; lane < kPeNum; ++lane) {
        if ((real_mask >> lane) & 1U) {
          lanes[static_cast<size_t>(lane)].pop_front();
        }
      }
    }

    ++cycles;
    if (!queues_drained && queues_empty(lanes)) {
      queues_drained = true;
      issue_complete_cycle = cycles;
    }
    if (queues_empty(lanes)) {
      break;
    }
  }

  if (cycles >= static_cast<size_t>(opt.timeout_cycles)) {
    std::cerr << "FAIL: timeout cycles=" << cycles
              << " issue_beats=" << issue_beats
              << " issues=" << issue_count << "\n";
    return 1;
  }

  const double issue_rate =
      issue_complete_cycle
          ? static_cast<double>(issue_count) /
                static_cast<double>(issue_complete_cycle)
          : 0.0;
  const double real_rate =
      issue_complete_cycle
          ? static_cast<double>(real_issue_count) /
                static_cast<double>(issue_complete_cycle)
          : 0.0;

  std::cout << "PASS: scoreboard dataset trace\n";
  std::cout << "  mode=direct-owner matrix=" << opt.matrix
            << " rows=" << csr.rows << " cols=" << csr.cols
            << " owner=" << opt.owner << "\n";
  std::cout << "  input_tokens=" << (initial_real_tokens + initial_done_tokens)
            << " real_tokens=" << initial_real_tokens
            << " done_tokens=" << initial_done_tokens << "\n";
  std::cout << "  issue_complete_cycle=" << issue_complete_cycle
            << " drain_complete_cycle=" << cycles
            << " issue_beats=" << issue_beats
            << " padding_beats=" << padding_beats
            << " issues=" << issue_count
            << " real_issues=" << real_issue_count
            << " done_issues=" << done_issue_count << "\n";
  std::cout << "  issue_rate=" << std::fixed << std::setprecision(4)
            << issue_rate << " tokens/cycle"
            << " real_rate=" << real_rate << " real/cycle"
            << " bypass_cycles=" << bypass_cycles << "\n";
  std::cout << "  lane_input_tokens:";
  for (size_t value : initial_lane_tokens) {
    std::cout << " " << value;
  }
  std::cout << "\n  lane_issued:";
  for (size_t value : issued_per_lane) {
    std::cout << " " << value;
  }
  std::cout << "\n";

  return 0;
}

int run_fifo_splitter(const Options& opt, const Csr& csr,
                      const SourceBeats& sources) {
  if (opt.fifo_depth <= 0) {
    std::cerr << "FAIL: fifo-depth must be positive in FIFO mode\n";
    return 2;
  }

  OwnerFifos fifos;
  std::array<size_t, kHbmChannels> source_pos{};
  std::array<size_t, kHbmChannels> source_stalls{};
  std::array<size_t, kHbmChannels> source_fires{};
  std::array<std::array<size_t, kPeNum>, kHbmChannels> lane_input_tokens{};
  std::array<std::array<size_t, kPeNum>, kHbmChannels> lane_issued{};
  std::array<std::array<size_t, kPeNum>, kHbmChannels> max_fifo_occupancy{};
  std::array<std::array<size_t, kPeNum>, kHbmChannels> full_stalls{};

  size_t input_tokens = 0;
  size_t real_tokens = 0;
  size_t done_tokens = 0;
  size_t total_source_beats = 0;
  size_t data_source_beats = 0;
  size_t padding_slots = 0;
  for (const auto& source_beats : sources) {
    total_source_beats += source_beats.size();
    for (const SplitBeat& beat : source_beats) {
      size_t valid_slots = 0;
      for (int slot_lane = 0; slot_lane < kPeNum; ++slot_lane) {
        if (!beat.valid[static_cast<size_t>(slot_lane)]) {
          continue;
        }
        ++valid_slots;
        const TaggedScalar& tagged = beat.token[static_cast<size_t>(slot_lane)];
        const int owner = static_cast<int>(owner_from_packet(tagged.packet_idx));
        const int lane = static_cast<int>(tagged.pair_lane);
        ++lane_input_tokens[static_cast<size_t>(owner)]
                           [static_cast<size_t>(lane)];
        ++input_tokens;
        if (tagged.done) {
          ++done_tokens;
        } else {
          ++real_tokens;
        }
      }
      if (!beat.done_beat) {
        ++data_source_beats;
        padding_slots += static_cast<size_t>(kPeNum) - valid_slots;
      }
    }
  }

  std::ofstream csv;
  if (!opt.csv_path.empty()) {
    csv.open(opt.csv_path);
    if (!csv) {
      std::cerr << "FAIL: failed to open csv " << opt.csv_path << "\n";
      return 1;
    }
    csv << "cycle,event,owner,lane,done,packet_idx,addr,is_pong,pair_lane,"
           "scalar_lane,source,slot_lane,batch,local_col,value,hazard_mask,"
           "issue_mask,fifo_occupancy\n";
  }

  VerilatedContext context;
  Scoreboards duts;
  for (int owner = 0; owner < kHbmChannels; ++owner) {
    duts[static_cast<size_t>(owner)] =
        std::make_unique<VCuperSpmvOnly_RtlIssueScoreboard8>();
    duts[static_cast<size_t>(owner)]->issue_ready = 1;
    duts[static_cast<size_t>(owner)]->rst = 1;
    drive_heads(*duts[static_cast<size_t>(owner)],
                fifos[static_cast<size_t>(owner)]);
  }
  for (int i = 0; i < 3; ++i) {
    eval_all(duts, context, 0);
    eval_all(duts, context, 1);
  }
  for (auto& dut : duts) {
    dut->rst = 0;
  }

  size_t cycles = 0;
  size_t issue_beats = 0;
  size_t padding_beats = 0;
  size_t issue_count = 0;
  size_t real_issue_count = 0;
  size_t done_issue_count = 0;
  size_t bypass_cycles = 0;
  size_t source_stall_cycles = 0;
  size_t any_source_stall_cycles = 0;
  size_t issue_complete_cycle = 0;
  size_t trace_events = 0;
  bool queues_drained = false;

  while (cycles < static_cast<size_t>(opt.timeout_cycles)) {
    for (int owner = 0; owner < kHbmChannels; ++owner) {
      drive_heads(*duts[static_cast<size_t>(owner)],
                  fifos[static_cast<size_t>(owner)]);
    }
    eval_all(duts, context, 0);

    std::array<uint8_t, kHbmChannels> issued_mask{};
    std::array<uint8_t, kHbmChannels> hazard_mask{};

    for (int owner = 0; owner < kHbmChannels; ++owner) {
      auto& dut = *duts[static_cast<size_t>(owner)];
      hazard_mask[static_cast<size_t>(owner)] = dut.lane_hazard;
      const bool any_valid = dut.head_valid != 0;
      const bool issue = dut.issue_valid && dut.issue_ready;
      const uint8_t real_mask = issue ? issue_real_mask(dut) : 0;
      if (any_valid && (dut.lane_hazard & dut.head_valid) != 0 && issue) {
        ++bypass_cycles;
      }
      if (issue) {
        ++issue_beats;
        if (real_mask == 0) {
          ++padding_beats;
        }
        issued_mask[static_cast<size_t>(owner)] = real_mask;
      }
    }

    eval_all(duts, context, 1);

    for (int owner = 0; owner < kHbmChannels; ++owner) {
      const uint8_t real_mask = issued_mask[static_cast<size_t>(owner)];
      if (real_mask == 0) {
        continue;
      }

      for (int lane = 0; lane < kPeNum; ++lane) {
        if (((real_mask >> lane) & 1U) == 0) {
          continue;
        }

        auto& fifo =
            fifos[static_cast<size_t>(owner)][static_cast<size_t>(lane)];
        const TaggedScalar issued = fifo.front();
        fifo.pop_front();

        ++issue_count;
        ++lane_issued[static_cast<size_t>(owner)][static_cast<size_t>(lane)];
        if (issued.done) {
          ++done_issue_count;
        } else {
          ++real_issue_count;
        }

        if (owner == opt.owner && static_cast<int>(trace_events) < opt.trace) {
          ++trace_events;
          std::cout << "trace cycle=" << cycles
                  << " owner=" << owner
                  << " mask=0x" << std::hex << static_cast<int>(real_mask)
                  << std::dec
                  << " lane=" << lane
                  << " done=" << issued.done
                  << " packet=" << issued.packet_idx
                  << " addr=" << issued.addr()
                  << " pingpong=" << (issued.is_pong() ? "pong" : "ping")
                  << " pair=" << issued.pair_lane
                  << " scalar=" << issued.scalar_lane
                  << " source=" << issued.source
                  << " slot_lane=" << issued.slot_lane
                  << " batch=" << issued.batch
                  << " local_col=" << issued.local_col
                  << " value=" << issued.value
                  << " hazard=0x" << std::hex
                  << static_cast<int>(hazard_mask[static_cast<size_t>(owner)])
                  << std::dec << "\n";
        }

        if (csv) {
          csv << cycles << ",issue," << owner << "," << lane << ","
            << issued.done << "," << issued.packet_idx << ","
            << issued.addr() << "," << issued.is_pong() << ","
            << issued.pair_lane << "," << issued.scalar_lane << ","
            << issued.source << "," << issued.slot_lane << ","
            << issued.batch << "," << issued.local_col << ","
            << std::setprecision(9) << issued.value << ",0x" << std::hex
            << static_cast<int>(hazard_mask[static_cast<size_t>(owner)])
            << ",0x"
            << static_cast<int>(real_mask)
            << std::dec << "," << fifo.size() << "\n";
        }
      }
    }

    bool any_source_stall = false;
    for (int source = 0; source < kHbmChannels; ++source) {
      const size_t pos = source_pos[static_cast<size_t>(source)];
      if (pos >= sources[static_cast<size_t>(source)].size()) {
        continue;
      }

      const SplitBeat& beat = sources[static_cast<size_t>(source)][pos];
      int blocked_owner = -1;
      int blocked_lane = -1;
      if (can_enqueue_beat(beat, fifos, opt.fifo_depth, &blocked_owner,
                           &blocked_lane)) {
        enqueue_beat(beat, fifos, max_fifo_occupancy);
        ++source_pos[static_cast<size_t>(source)];
        ++source_fires[static_cast<size_t>(source)];
      } else {
        ++source_stall_cycles;
        ++source_stalls[static_cast<size_t>(source)];
        any_source_stall = true;
        if (blocked_owner >= 0 && blocked_lane >= 0) {
          ++full_stalls[static_cast<size_t>(blocked_owner)]
                       [static_cast<size_t>(blocked_lane)];
        }
      }
    }
    if (any_source_stall) {
      ++any_source_stall_cycles;
    }

    ++cycles;
    if (!queues_drained && all_sources_done(sources, source_pos) &&
        all_fifos_empty(fifos)) {
      queues_drained = true;
      issue_complete_cycle = cycles;
    }
    if (all_sources_done(sources, source_pos) && all_fifos_empty(fifos)) {
      break;
    }
  }

  if (cycles >= static_cast<size_t>(opt.timeout_cycles)) {
    std::cerr << "FAIL: timeout cycles=" << cycles
              << " issue_beats=" << issue_beats
              << " issues=" << issue_count
              << " source_stall_cycles=" << source_stall_cycles << "\n";
    return 1;
  }

  size_t max_occ = 0;
  int max_occ_owner = 0;
  int max_occ_lane = 0;
  size_t max_full_stalls = 0;
  int max_full_owner = 0;
  int max_full_lane = 0;
  for (int owner = 0; owner < kHbmChannels; ++owner) {
    for (int lane = 0; lane < kPeNum; ++lane) {
      const size_t occ =
          max_fifo_occupancy[static_cast<size_t>(owner)]
                            [static_cast<size_t>(lane)];
      if (occ > max_occ) {
        max_occ = occ;
        max_occ_owner = owner;
        max_occ_lane = lane;
      }
      const size_t stalls =
          full_stalls[static_cast<size_t>(owner)][static_cast<size_t>(lane)];
      if (stalls > max_full_stalls) {
        max_full_stalls = stalls;
        max_full_owner = owner;
        max_full_lane = lane;
      }
    }
  }

  size_t max_source_stalls = source_stalls[0];
  int max_stall_source = 0;
  size_t min_source_fires = source_fires[0];
  int min_fire_source = 0;
  for (int source = 0; source < kHbmChannels; ++source) {
    if (source_stalls[static_cast<size_t>(source)] > max_source_stalls) {
      max_source_stalls = source_stalls[static_cast<size_t>(source)];
      max_stall_source = source;
    }
    if (source_fires[static_cast<size_t>(source)] < min_source_fires) {
      min_source_fires = source_fires[static_cast<size_t>(source)];
      min_fire_source = source;
    }
  }

  const double issue_rate_total =
      issue_complete_cycle
          ? static_cast<double>(issue_count) /
                static_cast<double>(issue_complete_cycle)
          : 0.0;
  const double issue_rate_per_owner =
      issue_complete_cycle
          ? static_cast<double>(issue_count) /
                (static_cast<double>(issue_complete_cycle) * kHbmChannels)
          : 0.0;
  const double real_rate_per_owner =
      issue_complete_cycle
          ? static_cast<double>(real_issue_count) /
                (static_cast<double>(issue_complete_cycle) * kHbmChannels)
          : 0.0;
  const double source_fire_rate =
      cycles ? static_cast<double>(total_source_beats) /
                   static_cast<double>(cycles * kHbmChannels)
             : 0.0;

  std::cout << "PASS: scoreboard dataset trace\n";
  std::cout << "  mode=fifo-splitter matrix=" << opt.matrix
            << " rows=" << csr.rows << " cols=" << csr.cols
            << " fifo_depth=" << opt.fifo_depth
            << " trace_owner=" << opt.owner << "\n";
  std::cout << "  input_tokens=" << input_tokens
            << " real_tokens=" << real_tokens
            << " done_tokens=" << done_tokens
            << " source_beats=" << total_source_beats
            << " data_source_beats=" << data_source_beats
            << " padding_slots=" << padding_slots << "\n";
  std::cout << "  issue_complete_cycle=" << issue_complete_cycle
            << " drain_complete_cycle=" << cycles
            << " issue_beats=" << issue_beats
            << " padding_beats=" << padding_beats
            << " issues=" << issue_count
            << " real_issues=" << real_issue_count
            << " done_issues=" << done_issue_count << "\n";
  std::cout << "  issue_rate_total=" << std::fixed << std::setprecision(4)
            << issue_rate_total << " tokens/cycle"
            << " issue_rate_per_owner=" << issue_rate_per_owner
            << " real_rate_per_owner=" << real_rate_per_owner
            << " source_fire_rate=" << source_fire_rate << "\n";
  std::cout << "  bypass_cycles=" << bypass_cycles
            << " source_stall_cycles=" << source_stall_cycles
            << " any_source_stall_cycles=" << any_source_stall_cycles
            << "\n";
  std::cout << "  max_fifo_occupancy=" << max_occ
            << " at owner=" << max_occ_owner
            << " lane=" << max_occ_lane
            << " max_full_stalls=" << max_full_stalls
            << " at owner=" << max_full_owner
            << " lane=" << max_full_lane << "\n";
  std::cout << "  max_stall_source=" << max_stall_source
            << " source_stalls=" << max_source_stalls
            << " min_fire_source=" << min_fire_source
            << " source_fires=" << min_source_fires << "\n";
  std::cout << "  trace_owner_lane_input_tokens:";
  for (size_t value : lane_input_tokens[static_cast<size_t>(opt.owner)]) {
    std::cout << " " << value;
  }
  std::cout << "\n  trace_owner_lane_issued:";
  for (size_t value : lane_issued[static_cast<size_t>(opt.owner)]) {
    std::cout << " " << value;
  }
  std::cout << "\n  trace_owner_max_fifo_occupancy:";
  for (size_t value : max_fifo_occupancy[static_cast<size_t>(opt.owner)]) {
    std::cout << " " << value;
  }
  std::cout << "\n";

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parse_options(argc, argv);

  Csr csr;
  BatchedLaneLists batches;
  try {
    csr = read_csr(opt.matrix);
    batches = make_batched_lane_lists(csr);
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: " << ex.what() << "\n";
    return 1;
  }

  try {
    if (opt.fifo_depth > 0) {
      return run_fifo_splitter(opt, csr, make_source_beats(batches));
    }
    return run_direct_owner(opt, csr,
                            make_owner_lane_streams(batches, opt.owner));
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: " << ex.what() << "\n";
    return 1;
  }
}
