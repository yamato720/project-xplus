#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kHbmChannels = 16;
constexpr int kPeNum = 8;
constexpr int kRowHbmNum = 4;
constexpr int kSliceSize = kHbmChannels * kRowHbmNum;
constexpr int kBatchSize = 8192 / kSliceSize;
constexpr int kPairNum = 8;
constexpr int kAccGroupSize = kHbmChannels / kPairNum;

struct CsrMatrix {
  int rows = 0;
  int cols = 0;
  std::vector<int> row_ptr;
  std::vector<int> col_idx;
  std::vector<float> values;
};

struct SpElement {
  int col = -1;
  int row = -1;
  float value = 0.0f;
};

struct SliceBlock {
  int row_slice = 0;
  std::vector<SpElement> elems;
};

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::string JoinPath(const std::string& dir, const std::string& name) {
  return (fs::path(dir) / name).string();
}

void Die(const std::string& message) {
  throw std::runtime_error(message);
}

std::vector<int> ReadInts(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    Die("failed to open " + path + ": " + std::strerror(errno));
  }
  std::vector<int> out;
  int value = 0;
  while (in >> value) {
    out.push_back(value);
  }
  return out;
}

std::vector<float> ReadFloats(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    Die("failed to open " + path + ": " + std::strerror(errno));
  }
  std::vector<float> out;
  double value = 0.0;
  while (in >> value) {
    out.push_back(static_cast<float>(value));
  }
  return out;
}

CsrMatrix ReadCsr(const std::string& matrix_dir) {
  CsrMatrix csr;
  csr.row_ptr = ReadInts(JoinPath(matrix_dir, "row_ptr.txt"));
  csr.col_idx = ReadInts(JoinPath(matrix_dir, "col_idx.txt"));
  csr.values = ReadFloats(JoinPath(matrix_dir, "values.txt"));
  if (csr.row_ptr.size() < 2) {
    Die("row_ptr.txt must contain at least two entries");
  }
  csr.rows = static_cast<int>(csr.row_ptr.size()) - 1;
  csr.cols = csr.rows;
  const int nnz = csr.row_ptr.back();
  if (csr.row_ptr.front() != 0) {
    Die("row_ptr[0] must be 0");
  }
  if (nnz < 0 || static_cast<size_t>(nnz) != csr.col_idx.size() ||
      static_cast<size_t>(nnz) != csr.values.size()) {
    Die("CSR nnz mismatch between row_ptr/col_idx/values");
  }
  return csr;
}

bool CompareColRow(const SpElement& lhs, const SpElement& rhs) {
  if (lhs.col != rhs.col) {
    return lhs.col < rhs.col;
  }
  return lhs.row < rhs.row;
}

int MapRowToPe(int row) {
  const int packet_id = row / 2;
  const int checker_id = packet_id % kPairNum;
  const int acc_offset = (packet_id / kPairNum) % kAccGroupSize;
  const int pe_in_acc = (packet_id / kHbmChannels) % kPeNum;
  return (checker_id * kAccGroupSize + acc_offset) * kPeNum + pe_in_acc;
}

SpElement EncodeDense(const SpElement& sp, int base_col, int num_pe) {
  const int org_row = sp.row / (2 * num_pe);
  return SpElement{sp.col - base_col, org_row * 2 + (sp.row & 1), sp.value};
}

uint64_t PackSpElement(const SpElement& sp) {
  if (sp.row < 0) {
    return 0x3FFFFULL << 32;
  }
  const uint64_t val = static_cast<uint64_t>(FloatBits(sp.value));
  const uint64_t row = (static_cast<uint64_t>(sp.row) & 0x3FFFFULL) << 32;
  const uint64_t col = (static_cast<uint64_t>(sp.col) & 0x3FFFULL) << 50;
  return col | row | val;
}

std::vector<std::vector<SliceBlock>> CreateSparseSlices(const CsrMatrix& csr) {
  const int num_col_slices = (csr.cols + kSliceSize - 1) / kSliceSize;
  const int num_row_slices = (csr.rows + kSliceSize - 1) / kSliceSize;
  std::vector<std::vector<SliceBlock>> cols(num_col_slices);

  for (int row = 0; row < csr.rows; ++row) {
    for (int pos = csr.row_ptr[row]; pos < csr.row_ptr[row + 1]; ++pos) {
      const int col = csr.col_idx[pos];
      if (col < 0 || col >= csr.cols) {
        Die("column index is outside matrix range");
      }
      const int slice_col = col / kSliceSize;
      const int slice_row = row / kSliceSize;
      auto& blocks = cols[slice_col];
      auto iter = std::find_if(blocks.begin(), blocks.end(),
                               [slice_row](const SliceBlock& block) {
                                 return block.row_slice == slice_row;
                               });
      if (iter == blocks.end()) {
        blocks.push_back(SliceBlock{slice_row, {}});
        iter = std::prev(blocks.end());
      }
      iter->elems.push_back(SpElement{col, row, csr.values[pos]});
    }
  }

  for (auto& blocks : cols) {
    std::sort(blocks.begin(), blocks.end(),
              [](const SliceBlock& lhs, const SliceBlock& rhs) {
                return lhs.row_slice < rhs.row_slice;
              });
    for (auto& block : blocks) {
      std::sort(block.elems.begin(), block.elems.end(),
                [](const SpElement& lhs, const SpElement& rhs) {
                  if (lhs.row != rhs.row) {
                    return lhs.row < rhs.row;
                  }
                  return lhs.col < rhs.col;
                });
    }
  }
  (void)num_row_slices;
  return cols;
}

void WriteHex32(std::ofstream& out, uint32_t value) {
  out << std::hex << std::setw(8) << std::setfill('0') << value << "\n";
}

void WriteHex64(std::ofstream& out, uint64_t value) {
  out << std::hex << std::setw(16) << std::setfill('0') << value << "\n";
}

void WriteHex512(std::ofstream& out, const uint64_t words[8]) {
  out << std::hex << std::setfill('0');
  for (int lane = 7; lane >= 0; --lane) {
    out << std::setw(16) << words[lane];
  }
  out << "\n";
}

void WriteVectorHex32(const std::string& path, const std::vector<uint32_t>& data) {
  std::ofstream out(path);
  if (!out) {
    Die("failed to open " + path);
  }
  for (uint32_t value : data) {
    WriteHex32(out, value);
  }
}

void WriteXMem(const std::string& path, int words) {
  std::ofstream out(path);
  if (!out) {
    Die("failed to open " + path);
  }
  uint64_t beat[8];
  for (int i = 0; i < 8; ++i) {
    const uint64_t lo = FloatBits(1.0f);
    const uint64_t hi = FloatBits(1.0f);
    beat[i] = (hi << 32) | lo;
  }
  for (int i = 0; i < words; ++i) {
    WriteHex512(out, beat);
  }
}

void WriteMatrixMem(const std::string& path, const std::vector<uint64_t>& slots) {
  std::ofstream out(path);
  if (!out) {
    Die("failed to open " + path);
  }
  if (slots.empty()) {
    uint64_t empty[8] = {};
    WriteHex512(out, empty);
    return;
  }
  if ((slots.size() % 8) != 0) {
    Die("matrix slot vector size must be a multiple of 8");
  }
  uint64_t beat[8];
  for (size_t idx = 0; idx < slots.size(); idx += 8) {
    for (int lane = 0; lane < 8; ++lane) {
      beat[lane] = slots[idx + lane];
    }
    WriteHex512(out, beat);
  }
}

int AlignUp(int value, int alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

int ParsePositiveInt(const char* name, const char* text) {
  char* end = nullptr;
  errno = 0;
  long value = std::strtol(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' || value <= 0 ||
      value > 0x7fffffffL) {
    std::ostringstream oss;
    oss << "invalid " << name << ": " << text;
    Die(oss.str());
  }
  return static_cast<int>(value);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string matrix_dir;
    std::string out_dir = "build/top_xsim_vectors";
    int iteration_num = 1;

    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--matrix") == 0 && i + 1 < argc) {
        matrix_dir = argv[++i];
      } else if (std::strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
        out_dir = argv[++i];
      } else if (std::strcmp(argv[i], "--iteration-num") == 0 && i + 1 < argc) {
        iteration_num = ParsePositiveInt("--iteration-num", argv[++i]);
      } else {
        std::cerr << "usage: " << argv[0]
                  << " --matrix CSR_DIR --out-dir DIR [--iteration-num N]\n";
        return 2;
      }
    }
    if (matrix_dir.empty()) {
      Die("--matrix CSR_DIR is required");
    }

    fs::create_directories(out_dir);
    const CsrMatrix csr = ReadCsr(matrix_dir);
    const auto slices = CreateSparseSlices(csr);
    const int num_pe = kHbmChannels * kPeNum;
    const int batch_num =
        (static_cast<int>(slices.size()) + kBatchSize - 1) / kBatchSize;

    std::vector<int> matrix_len(kHbmChannels, 0);
    std::vector<int> ptr_by_channel(kHbmChannels * (batch_num + 1), 0);
    std::vector<std::vector<uint64_t>> matrix_slots(kHbmChannels);
    std::vector<float> expected(csr.rows, 0.0f);

    for (int row = 0; row < csr.rows; ++row) {
      float sum = 0.0f;
      for (int pos = csr.row_ptr[row]; pos < csr.row_ptr[row + 1]; ++pos) {
        sum += csr.values[pos];
      }
      expected[row] = sum;
    }

    for (int batch = 0; batch < batch_num; ++batch) {
      std::vector<std::vector<SpElement>> lane_lists(num_pe);
      const int slice_begin = batch * kBatchSize;
      const int slice_end =
          std::min((batch + 1) * kBatchSize, static_cast<int>(slices.size()));
      const int base_col = batch * kBatchSize * kSliceSize;

      for (int slice_col = slice_begin; slice_col < slice_end; ++slice_col) {
        for (const SliceBlock& block : slices[slice_col]) {
          for (const SpElement& raw : block.elems) {
            lane_lists[MapRowToPe(raw.row)].push_back(raw);
          }
        }
      }

      for (auto& list : lane_lists) {
        std::sort(list.begin(), list.end(), CompareColRow);
        for (SpElement& sp : list) {
          sp = EncodeDense(sp, base_col, num_pe);
          if (sp.col < 0 || sp.col >= (1 << 14)) {
            Die("lane-static local column exceeds 14 bits");
          }
          if (sp.row < 0 || sp.row >= (1 << 17)) {
            Die("lane-static row encoding exceeds valid range");
          }
        }
      }

      for (int channel = 0; channel < kHbmChannels; ++channel) {
        int channel_batch_len = 0;
        for (int lane = 0; lane < kPeNum; ++lane) {
          const int pe = channel * kPeNum + lane;
          channel_batch_len =
              std::max(channel_batch_len,
                       static_cast<int>(lane_lists[pe].size()));
        }
        const int local_start = matrix_len[channel];
        matrix_len[channel] += channel_batch_len;
        ptr_by_channel[channel * (batch_num + 1) + batch + 1] =
            matrix_len[channel];

        for (int offset = 0; offset < channel_batch_len; ++offset) {
          for (int lane = 0; lane < kPeNum; ++lane) {
            const int pe = channel * kPeNum + lane;
            const SpElement sp =
                (offset < static_cast<int>(lane_lists[pe].size()))
                    ? lane_lists[pe][offset]
                    : SpElement{-1, -1, 0.0f};
            matrix_slots[channel].push_back(PackSpElement(sp));
          }
        }
        (void)local_start;
      }
    }

    std::vector<uint32_t> ptr_payload;
    ptr_payload.reserve(kHbmChannels + kHbmChannels * (batch_num + 1));
    for (int channel = 0; channel < kHbmChannels; ++channel) {
      ptr_payload.push_back(static_cast<uint32_t>(matrix_len[channel]));
    }
    for (int boundary = 0; boundary <= batch_num; ++boundary) {
      for (int channel = 0; channel < kHbmChannels; ++channel) {
        ptr_payload.push_back(static_cast<uint32_t>(
            ptr_by_channel[channel * (batch_num + 1) + boundary]));
      }
    }
    const int ptr_words = std::max(1024, AlignUp(static_cast<int>(ptr_payload.size()), 1024));
    ptr_payload.resize(ptr_words, 0);
    WriteVectorHex32(JoinPath(out_dir, "ptr.mem"), ptr_payload);

    for (int channel = 0; channel < kHbmChannels; ++channel) {
      const int channel_words = std::max(8, AlignUp(static_cast<int>(matrix_slots[channel].size()), 512));
      matrix_slots[channel].resize(channel_words, 0);
      std::ostringstream name;
      name << "matrix" << std::setw(2) << std::setfill('0') << channel << ".mem";
      WriteMatrixMem(JoinPath(out_dir, name.str()), matrix_slots[channel]);
    }

    const int x_words = std::max(64, AlignUp((csr.cols + 15) / 16, 64));
    WriteXMem(JoinPath(out_dir, "x.mem"), x_words);

    std::vector<uint32_t> expected_bits;
    expected_bits.reserve(expected.size());
    for (float value : expected) {
      expected_bits.push_back(FloatBits(value));
    }
    WriteVectorHex32(JoinPath(out_dir, "expected_y.mem"), expected_bits);

    {
      std::ofstream out(JoinPath(out_dir, "status_init.mem"));
      for (int i = 0; i < 16; ++i) {
        WriteHex32(out, 0x51510000u + static_cast<uint32_t>(i));
      }
    }
    {
      std::ofstream out(JoinPath(out_dir, "metrics_init.mem"));
      for (int i = 0; i < 16; ++i) {
        double value = -1000000.0 - static_cast<double>(i);
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
        std::memcpy(&bits, &value, sizeof(bits));
        WriteHex64(out, bits);
      }
    }

    int matrix_len_max = 0;
    int matrix_len_total = 0;
    for (int len : matrix_len) {
      matrix_len_max = std::max(matrix_len_max, len);
      matrix_len_total += len;
    }

    std::ofstream meta(JoinPath(out_dir, "meta.env"));
    meta << "ROWS=" << csr.rows << "\n";
    meta << "COLS=" << csr.cols << "\n";
    meta << "NNZ=" << csr.values.size() << "\n";
    meta << "BATCH_NUM=" << batch_num << "\n";
    meta << "MATRIX_LEN=" << matrix_len_max << "\n";
    meta << "MATRIX_LEN_TOTAL=" << matrix_len_total << "\n";
    meta << "ITERATION_NUM=" << iteration_num << "\n";
    meta << "PTR_WORDS=" << ptr_words << "\n";
    meta << "X_WORDS=" << x_words << "\n";
    meta << "Y_WORDS=" << std::max(1024, AlignUp(csr.rows, 1024)) << "\n";
    for (int channel = 0; channel < kHbmChannels; ++channel) {
      meta << "MATRIX_WORDS_" << channel << "="
           << std::max(1, static_cast<int>(matrix_slots[channel].size() / 8))
           << "\n";
      meta << "MATRIX_LEN_" << channel << "=" << matrix_len[channel] << "\n";
    }
    meta.close();

    std::cout << "generated top xsim vectors rows=" << csr.rows
              << " cols=" << csr.cols
              << " nnz=" << csr.values.size()
              << " batch_num=" << batch_num
              << " matrix_len_max=" << matrix_len_max
              << " matrix_len_total=" << matrix_len_total << "\n";
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
