#include "VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.h"
#include "VCuperSpmvOnly_SourceLaneSplitterOoo.h"
#include "VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.h"

#include "verilated.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kHbmChannels = 16;
constexpr int kPeNum = 8;
constexpr int kOwnerLaneStreams = kHbmChannels * kPeNum;
constexpr int kSliceSize = kHbmChannels * 4;
constexpr int kBatchSize = 8192 / kSliceSize;
constexpr int kTimeoutDefault = 1000000;
constexpr double kTolerance = 1.0e-3;

using SplitterTop = VCuperSpmvOnly_SourceLaneSplitterOoo;
using BankTop = VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo;
using ScatterTop =
    VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter;

using Word33 = uint64_t;
using Word130 = std::array<uint32_t, 5>;
using Word129 = std::array<uint32_t, 5>;
using Word401 = std::array<uint32_t, 13>;

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
  csr.rows = static_cast<int>(csr.row_ptr.size()) - 1;
  csr.cols = csr.rows;
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
  const int acc_offset = (packet_id / 8) % (kHbmChannels / 8);
  const int pe_in_acc = (packet_id / kHbmChannels) % kPeNum;
  return (checker_id * (kHbmChannels / 8) + acc_offset) * kPeNum +
         pe_in_acc;
}

int owner_lane_index(int source, int lane) {
  return ((((lane * (kHbmChannels / 8)) + (source % (kHbmChannels / 8))) *
           kPeNum) +
          (source / (kHbmChannels / 8)));
}

Word401 pack_matrix_mult_word(const std::array<SpElem, kPeNum>& slots,
                              const std::array<bool, kPeNum>& valid) {
  Word401 word{};
  for (int lane = 0; lane < kPeNum; ++lane) {
    const int row = valid[static_cast<size_t>(lane)] ? slots[lane].row
                                                     : (1 << 17);
    const uint32_t value = valid[static_cast<size_t>(lane)]
                               ? fbits(slots[lane].value)
                               : 0U;
    set_bits(word, lane * 18, 18, static_cast<uint32_t>(row));
    set_bits(word, 144 + lane * 32, 32, value);
  }
  return word;
}

void generate_splitter_inputs(
    const Csr& csr,
    std::array<std::deque<Word33>, kHbmChannels>& param_fifo,
    std::array<std::deque<Word401>, kHbmChannels>& matrix_fifo,
    std::vector<float>& expected) {
  expected.assign(static_cast<size_t>(csr.rows), 0.0f);
  const int num_col_slices = (csr.cols + kSliceSize - 1) / kSliceSize;
  const int batch_num = (num_col_slices + kBatchSize - 1) / kBatchSize;
  std::array<int, kHbmChannels> cumulative{};

  for (int source = 0; source < kHbmChannels; ++source) {
    param_fifo[source].push_back(static_cast<Word33>(batch_num));
    param_fifo[source].push_back(static_cast<Word33>(csr.rows));
    param_fifo[source].push_back(1);
    param_fifo[source].push_back(0);
  }

  for (int batch = 0; batch < batch_num; ++batch) {
    std::array<std::vector<SpElem>, kOwnerLaneStreams> lane_lists;
    const int base_col = batch * kBatchSize * kSliceSize;
    const int col_begin = base_col;
    const int col_end =
        std::min(csr.cols, (batch + 1) * kBatchSize * kSliceSize);

    for (int row = 0; row < csr.rows; ++row) {
      for (int off = csr.row_ptr[row]; off < csr.row_ptr[row + 1]; ++off) {
        const int col = csr.col_idx[off];
        if (col < col_begin || col >= col_end) {
          continue;
        }
        const float value = csr.values[off];
        expected[static_cast<size_t>(row)] += value;
        const int pe_idx = map_row_to_pe(row);
        const int encoded_row =
            (row / (2 * kOwnerLaneStreams)) * 2 + (row & 1);
        lane_lists[static_cast<size_t>(pe_idx)].push_back(
            SpElem{col - base_col, encoded_row, value});
      }
    }

    for (auto& lane_list : lane_lists) {
      std::sort(lane_list.begin(), lane_list.end(),
                [](const SpElem& a, const SpElem& b) {
                  if (a.col != b.col) {
                    return a.col < b.col;
                  }
                  return a.row < b.row;
                });
    }

    for (int source = 0; source < kHbmChannels; ++source) {
      int batch_len = 0;
      for (int lane = 0; lane < kPeNum; ++lane) {
        batch_len = std::max(
            batch_len,
            static_cast<int>(
                lane_lists[static_cast<size_t>(source * kPeNum + lane)]
                    .size()));
      }

      for (int offset = 0; offset < batch_len; ++offset) {
        std::array<SpElem, kPeNum> slots{};
        std::array<bool, kPeNum> valid{};
        for (int lane = 0; lane < kPeNum; ++lane) {
          const auto& lane_list =
              lane_lists[static_cast<size_t>(source * kPeNum + lane)];
          if (offset < static_cast<int>(lane_list.size())) {
            slots[static_cast<size_t>(lane)] =
                lane_list[static_cast<size_t>(offset)];
            valid[static_cast<size_t>(lane)] = true;
          }
        }
        matrix_fifo[static_cast<size_t>(source)].push_back(
            pack_matrix_mult_word(slots, valid));
      }

      cumulative[static_cast<size_t>(source)] += batch_len;
      param_fifo[static_cast<size_t>(source)].push_back(
          static_cast<Word33>(cumulative[static_cast<size_t>(source)]));
    }
  }
}

void eval_splitter(SplitterTop& top, VerilatedContext& context, int clk) {
  top.ap_clk = clk;
  top.eval();
  context.timeInc(1);
}

void eval_bank(BankTop& top, VerilatedContext& context, int clk) {
  top.ap_clk = clk;
  top.eval();
  context.timeInc(1);
}

void eval_scatter(ScatterTop& top, VerilatedContext& context, int clk) {
  top.ap_clk = clk;
  top.eval();
  context.timeInc(1);
}

void clear_splitter_peek(SplitterTop& splitter) {
  Word401 zero401{};
  Word130 zero130{};
  splitter.Vector_Y_Param_peek_dout = 0;
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

void set_splitter_lane_full(SplitterTop& splitter, int lane, bool full_n) {
  switch (lane) {
    case 0: splitter.Owner_Lane_Stream_0_s_full_n = full_n; break;
    case 1: splitter.Owner_Lane_Stream_1_s_full_n = full_n; break;
    case 2: splitter.Owner_Lane_Stream_2_s_full_n = full_n; break;
    case 3: splitter.Owner_Lane_Stream_3_s_full_n = full_n; break;
    case 4: splitter.Owner_Lane_Stream_4_s_full_n = full_n; break;
    case 5: splitter.Owner_Lane_Stream_5_s_full_n = full_n; break;
    case 6: splitter.Owner_Lane_Stream_6_s_full_n = full_n; break;
    default: splitter.Owner_Lane_Stream_7_s_full_n = full_n; break;
  }
}

bool splitter_lane_write(const SplitterTop& splitter, int lane) {
  switch (lane) {
    case 0: return splitter.Owner_Lane_Stream_0_s_write;
    case 1: return splitter.Owner_Lane_Stream_1_s_write;
    case 2: return splitter.Owner_Lane_Stream_2_s_write;
    case 3: return splitter.Owner_Lane_Stream_3_s_write;
    case 4: return splitter.Owner_Lane_Stream_4_s_write;
    case 5: return splitter.Owner_Lane_Stream_5_s_write;
    case 6: return splitter.Owner_Lane_Stream_6_s_write;
    default: return splitter.Owner_Lane_Stream_7_s_write;
  }
}

Word130 splitter_lane_word(const SplitterTop& splitter, int lane) {
  switch (lane) {
    case 0: return read_word(splitter.Owner_Lane_Stream_0_s_din);
    case 1: return read_word(splitter.Owner_Lane_Stream_1_s_din);
    case 2: return read_word(splitter.Owner_Lane_Stream_2_s_din);
    case 3: return read_word(splitter.Owner_Lane_Stream_3_s_din);
    case 4: return read_word(splitter.Owner_Lane_Stream_4_s_din);
    case 5: return read_word(splitter.Owner_Lane_Stream_5_s_din);
    case 6: return read_word(splitter.Owner_Lane_Stream_6_s_din);
    default: return read_word(splitter.Owner_Lane_Stream_7_s_din);
  }
}

void clear_bank_peek(BankTop& bank) {
  Word130 zero130{};
  Word129 zero129{};
  drive_word(bank.Owner_Lane_Stream_0_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_1_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_2_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_3_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_4_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_5_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_6_peek_dout, zero130);
  drive_word(bank.Owner_Lane_Stream_7_peek_dout, zero130);
  bank.Owner_Lane_Stream_0_peek_empty_n = 0;
  bank.Owner_Lane_Stream_1_peek_empty_n = 0;
  bank.Owner_Lane_Stream_2_peek_empty_n = 0;
  bank.Owner_Lane_Stream_3_peek_empty_n = 0;
  bank.Owner_Lane_Stream_4_peek_empty_n = 0;
  bank.Owner_Lane_Stream_5_peek_empty_n = 0;
  bank.Owner_Lane_Stream_6_peek_empty_n = 0;
  bank.Owner_Lane_Stream_7_peek_empty_n = 0;
  drive_word(bank.Vector_Y_Tagged_Stream_peek, zero129);
}

void drive_bank_lane(BankTop& bank, int lane,
                     const std::deque<Word130>& fifo) {
  const bool valid = !fifo.empty();
  Word130 zero{};
  const Word130& word = valid ? fifo.front() : zero;
  switch (lane) {
    case 0:
      drive_word(bank.Owner_Lane_Stream_0_s_dout, word);
      bank.Owner_Lane_Stream_0_s_empty_n = valid;
      break;
    case 1:
      drive_word(bank.Owner_Lane_Stream_1_s_dout, word);
      bank.Owner_Lane_Stream_1_s_empty_n = valid;
      break;
    case 2:
      drive_word(bank.Owner_Lane_Stream_2_s_dout, word);
      bank.Owner_Lane_Stream_2_s_empty_n = valid;
      break;
    case 3:
      drive_word(bank.Owner_Lane_Stream_3_s_dout, word);
      bank.Owner_Lane_Stream_3_s_empty_n = valid;
      break;
    case 4:
      drive_word(bank.Owner_Lane_Stream_4_s_dout, word);
      bank.Owner_Lane_Stream_4_s_empty_n = valid;
      break;
    case 5:
      drive_word(bank.Owner_Lane_Stream_5_s_dout, word);
      bank.Owner_Lane_Stream_5_s_empty_n = valid;
      break;
    case 6:
      drive_word(bank.Owner_Lane_Stream_6_s_dout, word);
      bank.Owner_Lane_Stream_6_s_empty_n = valid;
      break;
    default:
      drive_word(bank.Owner_Lane_Stream_7_s_dout, word);
      bank.Owner_Lane_Stream_7_s_empty_n = valid;
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

void drive_scatter_stream(ScatterTop& scatter, int channel,
                          const std::deque<Word129>& fifo) {
  const bool valid = !fifo.empty();
  Word129 zero{};
  const Word129& word = valid ? fifo.front() : zero;
  switch (channel) {
    case 0:
      drive_word(scatter.Vector_Y_Tagged_Stream_0_dout, word);
      scatter.Vector_Y_Tagged_Stream_0_empty_n = valid;
      break;
    case 1:
      drive_word(scatter.Vector_Y_Tagged_Stream_1_dout, word);
      scatter.Vector_Y_Tagged_Stream_1_empty_n = valid;
      break;
    case 2:
      drive_word(scatter.Vector_Y_Tagged_Stream_2_dout, word);
      scatter.Vector_Y_Tagged_Stream_2_empty_n = valid;
      break;
    case 3:
      drive_word(scatter.Vector_Y_Tagged_Stream_3_dout, word);
      scatter.Vector_Y_Tagged_Stream_3_empty_n = valid;
      break;
    case 4:
      drive_word(scatter.Vector_Y_Tagged_Stream_4_dout, word);
      scatter.Vector_Y_Tagged_Stream_4_empty_n = valid;
      break;
    case 5:
      drive_word(scatter.Vector_Y_Tagged_Stream_5_dout, word);
      scatter.Vector_Y_Tagged_Stream_5_empty_n = valid;
      break;
    case 6:
      drive_word(scatter.Vector_Y_Tagged_Stream_6_dout, word);
      scatter.Vector_Y_Tagged_Stream_6_empty_n = valid;
      break;
    case 7:
      drive_word(scatter.Vector_Y_Tagged_Stream_7_dout, word);
      scatter.Vector_Y_Tagged_Stream_7_empty_n = valid;
      break;
    case 8:
      drive_word(scatter.Vector_Y_Tagged_Stream_8_dout, word);
      scatter.Vector_Y_Tagged_Stream_8_empty_n = valid;
      break;
    case 9:
      drive_word(scatter.Vector_Y_Tagged_Stream_9_dout, word);
      scatter.Vector_Y_Tagged_Stream_9_empty_n = valid;
      break;
    case 10:
      drive_word(scatter.Vector_Y_Tagged_Stream_10_dout, word);
      scatter.Vector_Y_Tagged_Stream_10_empty_n = valid;
      break;
    case 11:
      drive_word(scatter.Vector_Y_Tagged_Stream_11_dout, word);
      scatter.Vector_Y_Tagged_Stream_11_empty_n = valid;
      break;
    case 12:
      drive_word(scatter.Vector_Y_Tagged_Stream_12_dout, word);
      scatter.Vector_Y_Tagged_Stream_12_empty_n = valid;
      break;
    case 13:
      drive_word(scatter.Vector_Y_Tagged_Stream_13_dout, word);
      scatter.Vector_Y_Tagged_Stream_13_empty_n = valid;
      break;
    case 14:
      drive_word(scatter.Vector_Y_Tagged_Stream_14_dout, word);
      scatter.Vector_Y_Tagged_Stream_14_empty_n = valid;
      break;
    default:
      drive_word(scatter.Vector_Y_Tagged_Stream_15_dout, word);
      scatter.Vector_Y_Tagged_Stream_15_empty_n = valid;
      break;
  }
}

bool scatter_stream_read(const ScatterTop& scatter, int channel) {
  switch (channel) {
    case 0: return scatter.Vector_Y_Tagged_Stream_0_read;
    case 1: return scatter.Vector_Y_Tagged_Stream_1_read;
    case 2: return scatter.Vector_Y_Tagged_Stream_2_read;
    case 3: return scatter.Vector_Y_Tagged_Stream_3_read;
    case 4: return scatter.Vector_Y_Tagged_Stream_4_read;
    case 5: return scatter.Vector_Y_Tagged_Stream_5_read;
    case 6: return scatter.Vector_Y_Tagged_Stream_6_read;
    case 7: return scatter.Vector_Y_Tagged_Stream_7_read;
    case 8: return scatter.Vector_Y_Tagged_Stream_8_read;
    case 9: return scatter.Vector_Y_Tagged_Stream_9_read;
    case 10: return scatter.Vector_Y_Tagged_Stream_10_read;
    case 11: return scatter.Vector_Y_Tagged_Stream_11_read;
    case 12: return scatter.Vector_Y_Tagged_Stream_12_read;
    case 13: return scatter.Vector_Y_Tagged_Stream_13_read;
    case 14: return scatter.Vector_Y_Tagged_Stream_14_read;
    default: return scatter.Vector_Y_Tagged_Stream_15_read;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string matrix_dir = "../data/suitesparse/Schmid/csr/thermal2_n1024";
  int timeout_cycles = kTimeoutDefault;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--matrix" && i + 1 < argc) {
      matrix_dir = argv[++i];
    } else if (arg == "--timeout-cycles" && i + 1 < argc) {
      timeout_cycles = std::stoi(argv[++i]);
    } else {
      std::cerr << "usage: " << argv[0]
                << " [--matrix CSR_DIR] [--timeout-cycles N]\n";
      return 2;
    }
  }

  Csr csr;
  std::array<std::deque<Word33>, kHbmChannels> param_fifo;
  std::array<std::deque<Word401>, kHbmChannels> matrix_fifo;
  std::vector<float> expected;
  try {
    csr = read_csr(matrix_dir);
    generate_splitter_inputs(csr, param_fifo, matrix_fifo, expected);
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: " << ex.what() << "\n";
    return 1;
  }

  const int num_out_packets = (csr.rows + 15) / 16;
  const int tagged_pairs_total = num_out_packets * 8;
  const int scalar_writes_total = tagged_pairs_total * 2;
  std::array<int, kHbmChannels> matrix_words{};
  int total_matrix_words = 0;
  for (int source = 0; source < kHbmChannels; ++source) {
    matrix_words[source] = static_cast<int>(matrix_fifo[source].size());
    total_matrix_words += matrix_words[source];
  }

  std::cout << "INFO: splitter16-bank16 dataset matrix=" << matrix_dir
            << " rows=" << csr.rows
            << " nnz=" << csr.col_idx.size()
            << " batch_num=" << ((csr.cols + kSliceSize - 1) / kSliceSize +
                                  kBatchSize - 1) /
                                     kBatchSize
            << " matrix_words=" << total_matrix_words
            << " tagged_pairs=" << tagged_pairs_total
            << " scalar_writes=" << scalar_writes_total << "\n";

  VerilatedContext context;
  context.commandArgs(argc, argv);
  std::array<std::unique_ptr<SplitterTop>, kHbmChannels> splitters;
  std::array<std::unique_ptr<BankTop>, kHbmChannels> banks;
  for (int i = 0; i < kHbmChannels; ++i) {
    splitters[i] = std::make_unique<SplitterTop>();
    banks[i] = std::make_unique<BankTop>();
  }
  ScatterTop scatter;

  std::array<std::deque<Word130>, kOwnerLaneStreams> owner_lane_fifo;
  std::array<std::deque<Word129>, kHbmChannels> bank_to_scatter_fifo;
  std::vector<uint32_t> y(static_cast<size_t>(scalar_writes_total), 0);
  std::vector<bool> y_seen(static_cast<size_t>(scalar_writes_total), false);

  std::array<int, kHbmChannels> splitter_param_reads{};
  std::array<int, kHbmChannels> splitter_matrix_reads{};
  std::array<int, kHbmChannels> splitter_writes{};
  std::array<int, kHbmChannels> bank_reads{};
  std::array<int, kHbmChannels> bank_pairs{};
  std::array<int, kHbmChannels> scatter_reads{};
  std::array<bool, kHbmChannels> splitter_done_seen{};
  std::array<bool, kHbmChannels> bank_done_seen{};
  bool scatter_done_seen = false;
  int y_write_count = 0;
  int pending_responses = 0;

  for (int cycle = 0; cycle < timeout_cycles; ++cycle) {
    const bool rst = cycle < 5;
    const bool response_available = !rst && pending_responses > 0;

    for (int source = 0; source < kHbmChannels; ++source) {
      auto& splitter = *splitters[source];
      splitter.ap_rst_n = rst ? 0 : 1;
      splitter.ap_start = (!rst && !splitter_done_seen[source]) ? 1 : 0;
      splitter.Source_id = static_cast<uint32_t>(source);
      clear_splitter_peek(splitter);

      if (!param_fifo[source].empty()) {
        splitter.Vector_Y_Param_s_dout = param_fifo[source].front();
        splitter.Vector_Y_Param_s_empty_n = 1;
      } else {
        splitter.Vector_Y_Param_s_dout = 0;
        splitter.Vector_Y_Param_s_empty_n = 0;
      }
      if (!matrix_fifo[source].empty()) {
        drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout,
                   matrix_fifo[source].front());
        splitter.Matrix_Mult_Vector_Stream_s_empty_n = 1;
      } else {
        Word401 zero{};
        drive_word(splitter.Matrix_Mult_Vector_Stream_s_dout, zero);
        splitter.Matrix_Mult_Vector_Stream_s_empty_n = 0;
      }
      for (int lane = 0; lane < kPeNum; ++lane) {
        const int stream_idx = owner_lane_index(source, lane);
        set_splitter_lane_full(
            splitter,
            lane,
            owner_lane_fifo[static_cast<size_t>(stream_idx)].size() < 256);
      }
    }

    for (int owner = 0; owner < kHbmChannels; ++owner) {
      auto& bank = *banks[owner];
      bank.ap_rst_n = rst ? 0 : 1;
      bank.ap_start = (!rst && !bank_done_seen[owner]) ? 1 : 0;
      bank.Iteration_num = 1;
      bank.Row_num = static_cast<uint32_t>(csr.rows);
      bank.Owner_id = static_cast<uint32_t>(owner);
      bank.Vector_Y_Tagged_Stream_s_full_n =
          bank_to_scatter_fifo[owner].size() < 256;
      clear_bank_peek(bank);
      for (int lane = 0; lane < kPeNum; ++lane) {
        drive_bank_lane(bank, lane, owner_lane_fifo[owner * kPeNum + lane]);
      }
    }

    scatter.ap_rst = rst ? 1 : 0;
    scatter.ap_start = (!rst && !scatter_done_seen) ? 1 : 0;
    scatter.scalar_writes_total = static_cast<uint32_t>(scalar_writes_total);
    scatter.tagged_pairs_total = static_cast<uint32_t>(tagged_pairs_total);
    scatter.Y_out_write_addr_s_full_n = 1;
    scatter.Y_out_write_data_s_full_n = 1;
    scatter.Y_out_write_addr_offset_load = 0;
    scatter.Y_out_write_resp_s_dout = 0;
    scatter.Y_out_write_resp_s_empty_n = response_available ? 1 : 0;
    for (int owner = 0; owner < kHbmChannels; ++owner) {
      drive_scatter_stream(scatter, owner, bank_to_scatter_fifo[owner]);
    }

    for (auto& splitter : splitters) eval_splitter(*splitter, context, 0);
    for (auto& bank : banks) eval_bank(*bank, context, 0);
    eval_scatter(scatter, context, 0);

    std::array<bool, kHbmChannels> param_read_pre{};
    std::array<bool, kHbmChannels> matrix_read_pre{};
    std::array<std::array<bool, kPeNum>, kHbmChannels> splitter_write_pre{};
    std::array<std::array<Word130, kPeNum>, kHbmChannels> splitter_word_pre{};
    std::array<std::array<bool, kPeNum>, kHbmChannels> bank_read_pre{};
    std::array<bool, kHbmChannels> bank_write_pre{};
    std::array<Word129, kHbmChannels> bank_word_pre{};
    std::array<bool, kHbmChannels> scatter_read_pre{};

    for (int source = 0; source < kHbmChannels; ++source) {
      const auto& splitter = *splitters[source];
      param_read_pre[source] =
          !rst && splitter.Vector_Y_Param_s_read && !param_fifo[source].empty();
      matrix_read_pre[source] =
          !rst && splitter.Matrix_Mult_Vector_Stream_s_read &&
          !matrix_fifo[source].empty();
      for (int lane = 0; lane < kPeNum; ++lane) {
        splitter_write_pre[source][lane] =
            !rst && splitter_lane_write(splitter, lane);
        if (splitter_write_pre[source][lane]) {
          splitter_word_pre[source][lane] = splitter_lane_word(splitter, lane);
        }
      }
    }

    for (int owner = 0; owner < kHbmChannels; ++owner) {
      const auto& bank = *banks[owner];
      for (int lane = 0; lane < kPeNum; ++lane) {
        bank_read_pre[owner][lane] =
            !rst && bank_lane_read(bank, lane) &&
            !owner_lane_fifo[owner * kPeNum + lane].empty();
      }
      bank_write_pre[owner] = !rst && bank.Vector_Y_Tagged_Stream_s_write;
      if (bank_write_pre[owner]) {
        bank_word_pre[owner] = read_word(bank.Vector_Y_Tagged_Stream_s_din);
      }
      scatter_read_pre[owner] = !rst && scatter_stream_read(scatter, owner);
    }
    const bool y_addr_write_pre = !rst && scatter.Y_out_write_addr_s_write;
    const bool y_data_write_pre = !rst && scatter.Y_out_write_data_s_write;
    const uint64_t y_addr_pre = scatter.Y_out_write_addr_s_din;
    const uint32_t y_data_pre =
        static_cast<uint32_t>(scatter.Y_out_write_data_s_din);
    const bool y_resp_read_pre = !rst && scatter.Y_out_write_resp_s_read;

    for (auto& splitter : splitters) eval_splitter(*splitter, context, 1);
    for (auto& bank : banks) eval_bank(*bank, context, 1);
    eval_scatter(scatter, context, 1);

    for (int source = 0; source < kHbmChannels; ++source) {
      if (param_read_pre[source]) {
        param_fifo[source].pop_front();
        ++splitter_param_reads[source];
      }
      if (matrix_read_pre[source]) {
        matrix_fifo[source].pop_front();
        ++splitter_matrix_reads[source];
      }
      for (int lane = 0; lane < kPeNum; ++lane) {
        if (splitter_write_pre[source][lane]) {
          if (splitter_writes[source] < 2 && csr.rows <= 16) {
            const Word130& tagged = splitter_word_pre[source][lane];
            const uint32_t done = tagged[0] & 1U;
            const uint32_t packet =
                (tagged[0] >> 1) | ((tagged[1] & 1U) << 31);
            const uint32_t pair =
                (tagged[1] >> 1) | ((tagged[2] & 1U) << 31);
            const uint32_t scalar =
                (tagged[2] >> 1) | ((tagged[3] & 1U) << 31);
            const uint32_t value =
                (tagged[3] >> 1) | ((tagged[4] & 1U) << 31);
            std::cerr << "TRACE split source=" << source
                      << " lane=" << lane
                      << " stream=" << owner_lane_index(source, lane)
                      << " done=" << done
                      << " packet=" << packet
                      << " pair=" << pair
                      << " scalar=" << scalar
                      << " value=" << fval(value) << "\n";
          }
          const int stream_idx = owner_lane_index(source, lane);
          owner_lane_fifo[static_cast<size_t>(stream_idx)].push_back(
              splitter_word_pre[source][lane]);
          ++splitter_writes[source];
        }
      }
      if (splitters[source]->ap_done) {
        splitter_done_seen[source] = true;
      }
    }

    for (int owner = 0; owner < kHbmChannels; ++owner) {
      for (int lane = 0; lane < kPeNum; ++lane) {
        if (bank_read_pre[owner][lane]) {
          owner_lane_fifo[owner * kPeNum + lane].pop_front();
          ++bank_reads[owner];
        }
      }
    if (bank_write_pre[owner]) {
        if (bank_pairs[owner] < 2 && csr.rows <= 16) {
          const Word129& tagged = bank_word_pre[owner];
          const uint32_t packet = tagged[0];
          const uint32_t pair = tagged[1];
          const uint32_t ping = tagged[2];
          const uint32_t pong = tagged[3];
          std::cerr << "TRACE bank owner=" << owner
                    << " packet=" << packet
                    << " pair=" << pair
                    << " ping=" << fval(ping)
                    << " pong=" << fval(pong) << "\n";
        }
        bank_to_scatter_fifo[owner].push_back(bank_word_pre[owner]);
        ++bank_pairs[owner];
      }
      if (banks[owner]->ap_done) {
        bank_done_seen[owner] = true;
      }
      if (scatter_read_pre[owner]) {
        if (bank_to_scatter_fifo[owner].empty()) {
          std::cerr << "FAIL: scatter read empty owner=" << owner << "\n";
          return 1;
        }
        bank_to_scatter_fifo[owner].pop_front();
        ++scatter_reads[owner];
      }
    }

    if (y_addr_write_pre || y_data_write_pre) {
      if (!(y_addr_write_pre && y_data_write_pre)) {
        std::cerr << "FAIL: split Y write addr=" << y_addr_write_pre
                  << " data=" << y_data_write_pre << "\n";
        return 1;
      }
      const uint64_t row = y_addr_pre >> 2;
      if (y_write_count < 20 && csr.rows <= 16) {
        std::cerr << "TRACE y_write row=" << row
                  << " raw_addr=" << y_addr_pre
                  << " value=" << fval(y_data_pre) << "\n";
      }
      if (row >= y.size()) {
        std::cerr << "FAIL: Y write row=" << row << " raw_addr=" << y_addr_pre
                  << " y_size=" << y.size() << "\n";
        return 1;
      }
      y[static_cast<size_t>(row)] = y_data_pre;
      y_seen[static_cast<size_t>(row)] = true;
      ++y_write_count;
      ++pending_responses;
    }
    if (y_resp_read_pre) {
      if (!response_available) {
        std::cerr << "FAIL: response read on empty stream\n";
        return 1;
      }
      --pending_responses;
    }

    if (!rst && scatter.ap_done) {
      scatter_done_seen = true;
      int errors = 0;
      int total_param_reads = 0;
      int total_matrix_reads = 0;
      int total_splitter_writes = 0;
      int total_bank_reads = 0;
      int total_bank_pairs = 0;
      int total_scatter_reads = 0;
      for (int source = 0; source < kHbmChannels; ++source) {
        total_param_reads += splitter_param_reads[source];
        total_matrix_reads += splitter_matrix_reads[source];
        total_splitter_writes += splitter_writes[source];
        total_bank_reads += bank_reads[source];
        total_bank_pairs += bank_pairs[source];
        total_scatter_reads += scatter_reads[source];
        if (!param_fifo[source].empty() || !matrix_fifo[source].empty()) {
          std::cerr << "FAIL: source " << source
                    << " param_left=" << param_fifo[source].size()
                    << " matrix_left=" << matrix_fifo[source].size() << "\n";
          ++errors;
        }
        if (!bank_to_scatter_fifo[source].empty()) {
          std::cerr << "FAIL: owner " << source
                    << " scatter_fifo_left="
                    << bank_to_scatter_fifo[source].size() << "\n";
          ++errors;
        }
      }
      for (int stream = 0; stream < kOwnerLaneStreams; ++stream) {
        if (!owner_lane_fifo[stream].empty()) {
          std::cerr << "FAIL: owner_lane_stream " << stream
                    << " left=" << owner_lane_fifo[stream].size() << "\n";
          ++errors;
        }
      }

      if (total_matrix_reads != total_matrix_words) {
        std::cerr << "FAIL: matrix_reads=" << total_matrix_reads
                  << " expect=" << total_matrix_words << "\n";
        ++errors;
      }
      if (total_bank_pairs != tagged_pairs_total) {
        std::cerr << "FAIL: bank_pairs=" << total_bank_pairs
                  << " expect=" << tagged_pairs_total << "\n";
        ++errors;
      }
      if (total_scatter_reads != tagged_pairs_total) {
        std::cerr << "FAIL: scatter_reads=" << total_scatter_reads
                  << " expect=" << tagged_pairs_total << "\n";
        ++errors;
      }
      if (y_write_count != scalar_writes_total) {
        std::cerr << "FAIL: y_writes=" << y_write_count
                  << " expect=" << scalar_writes_total << "\n";
        ++errors;
      }
      for (int row = 0; row < csr.rows; ++row) {
        const bool seen = y_seen[static_cast<size_t>(row)];
        const float got = fval(y[static_cast<size_t>(row)]);
        const float exp = expected[static_cast<size_t>(row)];
        const float diff = std::fabs(got - exp);
        const float denom = std::min(std::fabs(got), std::fabs(exp)) +
                            static_cast<float>(kTolerance);
        if (!seen || diff / denom > kTolerance) {
          if (errors < 16) {
            std::cerr << "FAIL: row=" << row << " seen=" << seen
                      << " got=" << got << " expect=" << exp
                      << " diff=" << diff << "\n";
          }
          ++errors;
        }
      }

      if (errors != 0) {
        std::cerr << "FAIL: dataset sim errors=" << errors
                  << " cycles=" << cycle
                  << " param_reads=" << total_param_reads
                  << " matrix_reads=" << total_matrix_reads
                  << " splitter_writes=" << total_splitter_writes
                  << " bank_reads=" << total_bank_reads
                  << " bank_pairs=" << total_bank_pairs
                  << " scatter_reads=" << total_scatter_reads
                  << " y_writes=" << y_write_count << "\n";
        return 1;
      }

      std::cout << "PASS: splitter16-bank16 dataset cycles=" << cycle
                << " param_reads=" << total_param_reads
                << " matrix_reads=" << total_matrix_reads
                << " splitter_writes=" << total_splitter_writes
                << " bank_reads=" << total_bank_reads
                << " bank_pairs=" << total_bank_pairs
                << " scatter_reads=" << total_scatter_reads
                << " y_writes=" << y_write_count << "\n";
      return 0;
    }
  }

  int total_matrix_reads = 0;
  int total_splitter_writes = 0;
  int total_bank_reads = 0;
  int total_bank_pairs = 0;
  int total_scatter_reads = 0;
  for (int i = 0; i < kHbmChannels; ++i) {
    total_matrix_reads += splitter_matrix_reads[i];
    total_splitter_writes += splitter_writes[i];
    total_bank_reads += bank_reads[i];
    total_bank_pairs += bank_pairs[i];
    total_scatter_reads += scatter_reads[i];
  }
  std::cerr << "FAIL: timeout cycles=" << timeout_cycles
            << " matrix_reads=" << total_matrix_reads << "/"
            << total_matrix_words
            << " splitter_writes=" << total_splitter_writes
            << " bank_reads=" << total_bank_reads
            << " bank_pairs=" << total_bank_pairs << "/"
            << tagged_pairs_total
            << " scatter_reads=" << total_scatter_reads
            << " y_writes=" << y_write_count << "/"
            << scalar_writes_total
            << " pending_responses=" << pending_responses << "\n";
  for (int i = 0; i < kHbmChannels; ++i) {
    std::cerr << "  source/owner " << i
              << " param_left=" << param_fifo[i].size()
              << " matrix_left=" << matrix_fifo[i].size()
              << " splitter_done=" << splitter_done_seen[i]
              << " bank_done=" << bank_done_seen[i]
              << " bank_fifo=" << bank_to_scatter_fifo[i].size()
              << " matrix_reads=" << splitter_matrix_reads[i]
              << " splitter_writes=" << splitter_writes[i]
              << " bank_reads=" << bank_reads[i]
              << " bank_pairs=" << bank_pairs[i]
              << " scatter_reads=" << scatter_reads[i] << "\n";
  }
  return 1;
}
