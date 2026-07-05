#include "VCuperSpmvChisel8.h"
#include "VCuperSpmvChisel8___024root.h"

#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kChannels = 8;
constexpr int kWideWords = 16;
constexpr uint32_t kStatusMagic = 0x43535056U;
constexpr uint32_t kSpmvMagic = 0x53504d56U;
constexpr uint64_t kMetricsMagic = 0x4353504d56384348ULL;

using Wide512 = std::array<uint32_t, kWideWords>;

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

template <size_t N>
void drive_wide(VlWide<N>& port, const std::array<uint32_t, N>& value) {
  for (int i = 0; i < static_cast<int>(N); ++i) {
    port[i] = value[static_cast<size_t>(i)];
  }
}

Wide512 zero_wide() {
  return {};
}

uint64_t pack_slot(int row, int col, float value) {
  if (row < 0 || col < 0) {
    return 0x3FFFFULL << 32;
  }
  return (static_cast<uint64_t>(col & 0x3fff) << 50) |
         (static_cast<uint64_t>(row & 0x3ffff) << 32) |
         static_cast<uint64_t>(float_bits(value));
}

Wide512 make_tiny_matrix_word() {
  Wide512 word{};
  const uint64_t slot0 = pack_slot(0, 0, 2.0f);
  word[0] = static_cast<uint32_t>(slot0);
  word[1] = static_cast<uint32_t>(slot0 >> 32);
  for (int lane = 1; lane < kChannels; ++lane) {
    const uint64_t padding = pack_slot(-1, -1, 0.0f);
    word[static_cast<size_t>(lane * 2)] = static_cast<uint32_t>(padding);
    word[static_cast<size_t>(lane * 2 + 1)] = static_cast<uint32_t>(padding >> 32);
  }
  return word;
}

Wide512 make_x_word() {
  Wide512 word{};
  for (int lane = 0; lane < 16; ++lane) {
    word[static_cast<size_t>(lane)] = float_bits(static_cast<float>(lane + 1));
  }
  return word;
}

struct Read32Port {
  std::vector<uint32_t> mem;
  bool rvalid = false;
  uint32_t rdata = 0;

  explicit Read32Port(std::vector<uint32_t> data) : mem(std::move(data)) {}

  uint32_t load(uint64_t addr) const {
    const size_t index = static_cast<size_t>(addr >> 2);
    return index < mem.size() ? mem[index] : 0;
  }
};

struct Read512Port {
  std::vector<Wide512> mem;
  bool rvalid = false;
  Wide512 rdata{};

  explicit Read512Port(std::vector<Wide512> data) : mem(std::move(data)) {}

  Wide512 load(uint64_t addr) const {
    const size_t index = static_cast<size_t>(addr >> 6);
    return index < mem.size() ? mem[index] : zero_wide();
  }
};

struct Write32Port {
  std::vector<uint32_t> mem;
  bool aw_pending = false;
  bool w_pending = false;
  bool bvalid = false;
  uint64_t awaddr = 0;
  uint32_t wdata = 0;

  explicit Write32Port(size_t words) : mem(words, 0) {}
};

struct Write64Port {
  std::vector<uint64_t> mem;
  bool aw_pending = false;
  bool w_pending = false;
  bool bvalid = false;
  uint64_t awaddr = 0;
  uint64_t wdata = 0;

  explicit Write64Port(size_t words) : mem(words, 0) {}
};

struct AxiSample {
  CData ptr_arvalid = 0;
  CData ptr_rready = 0;
  uint64_t ptr_araddr = 0;

  std::array<CData, kChannels> matrix_arvalid{};
  std::array<CData, kChannels> matrix_rready{};
  std::array<uint64_t, kChannels> matrix_araddr{};

  CData x_arvalid = 0;
  CData x_rready = 0;
  uint64_t x_araddr = 0;

  CData y_awvalid = 0;
  CData y_wvalid = 0;
  CData y_bready = 0;
  uint64_t y_awaddr = 0;
  uint32_t y_wdata = 0;

  CData status_awvalid = 0;
  CData status_wvalid = 0;
  CData status_bready = 0;
  uint64_t status_awaddr = 0;
  uint32_t status_wdata = 0;

  CData metrics_awvalid = 0;
  CData metrics_wvalid = 0;
  CData metrics_bready = 0;
  uint64_t metrics_awaddr = 0;
  uint64_t metrics_wdata = 0;
};

struct Sim {
  VerilatedContext context;
  VCuperSpmvChisel8 top;
  Read32Port ptr;
  std::array<Read512Port, kChannels> matrix;
  Read512Port x;
  Write32Port y;
  Write32Port status;
  Write64Port metrics;

  Sim()
      : ptr(make_ptr_mem()),
        matrix{Read512Port({make_tiny_matrix_word()}),
               Read512Port({}),
               Read512Port({}),
               Read512Port({}),
               Read512Port({}),
               Read512Port({}),
               Read512Port({}),
               Read512Port({})},
        x({make_x_word()}),
        y(32),
        status(64),
        metrics(64) {}

  static std::vector<uint32_t> make_ptr_mem() {
    std::vector<uint32_t> mem(24, 0);
    mem[0] = 1;
    for (int channel = 1; channel < kChannels; ++channel) {
      mem[static_cast<size_t>(channel)] = 0;
    }
    for (int channel = 0; channel < kChannels; ++channel) {
      mem[static_cast<size_t>(8 + channel)] = 0;
      mem[static_cast<size_t>(16 + channel)] = channel == 0 ? 1 : 0;
    }
    return mem;
  }

  void drive_read32(Read32Port& port,
                    CData& arready,
                    CData& rvalid,
                    IData& rdata,
                    CData& rid,
                    CData& rlast,
                    CData& rresp,
                    CData& awready,
                    CData& wready,
                    CData& bvalid,
                    CData& bid,
                    CData& bresp) {
    arready = !port.rvalid;
    rvalid = port.rvalid;
    rdata = port.rdata;
    rid = 0;
    rlast = 1;
    rresp = 0;
    awready = 1;
    wready = 1;
    bvalid = 0;
    bid = 0;
    bresp = 0;
  }

  void update_read32(Read32Port& port,
                     CData arvalid,
                     uint64_t araddr,
                     CData rready) {
    const bool old_rvalid = port.rvalid;
    const bool ar_fire = arvalid && !old_rvalid;
    const bool r_fire = old_rvalid && rready;
    if (r_fire) {
      port.rvalid = false;
    }
    if (ar_fire) {
      port.rdata = port.load(araddr);
      port.rvalid = true;
    }
  }

  void drive_read512(Read512Port& port,
                     CData& arready,
                     CData& rvalid,
                     VlWide<kWideWords>& rdata,
                     CData& rid,
                     CData& rlast,
                     CData& rresp,
                     CData& awready,
                     CData& wready,
                     CData& bvalid,
                     CData& bid,
                     CData& bresp) {
    arready = !port.rvalid;
    rvalid = port.rvalid;
    drive_wide(rdata, port.rdata);
    rid = 0;
    rlast = 1;
    rresp = 0;
    awready = 1;
    wready = 1;
    bvalid = 0;
    bid = 0;
    bresp = 0;
  }

  void update_read512(Read512Port& port,
                      CData arvalid,
                      uint64_t araddr,
                      CData rready) {
    const bool old_rvalid = port.rvalid;
    const bool ar_fire = arvalid && !old_rvalid;
    const bool r_fire = old_rvalid && rready;
    if (r_fire) {
      port.rvalid = false;
    }
    if (ar_fire) {
      port.rdata = port.load(araddr);
      port.rvalid = true;
    }
  }

  void drive_write32(Write32Port& port,
                     CData& awready,
                     CData& wready,
                     CData& bvalid,
                     CData& bid,
                     CData& bresp,
                     CData& arready,
                     CData& rvalid,
                     IData& rdata,
                     CData& rid,
                     CData& rlast,
                     CData& rresp) {
    awready = !port.aw_pending && !port.bvalid;
    wready = !port.w_pending && !port.bvalid;
    bvalid = port.bvalid;
    bid = 0;
    bresp = 0;
    arready = 1;
    rvalid = 0;
    rdata = 0;
    rid = 0;
    rlast = 1;
    rresp = 0;
  }

  void update_write32(Write32Port& port,
                      CData awvalid,
                      uint64_t awaddr,
                      CData wvalid,
                      uint32_t wdata,
                      CData bready) {
    const bool aw_fire = awvalid && !port.aw_pending && !port.bvalid;
    const bool w_fire = wvalid && !port.w_pending && !port.bvalid;
    const bool b_fire = port.bvalid && bready;
    if (b_fire) {
      port.bvalid = false;
    }
    if (aw_fire) {
      port.awaddr = awaddr;
      port.aw_pending = true;
    }
    if (w_fire) {
      port.wdata = wdata;
      port.w_pending = true;
    }
    if (port.aw_pending && port.w_pending && !port.bvalid) {
      const size_t index = static_cast<size_t>(port.awaddr >> 2);
      if (index >= port.mem.size()) {
        port.mem.resize(index + 1, 0);
      }
      port.mem[index] = port.wdata;
      port.aw_pending = false;
      port.w_pending = false;
      port.bvalid = true;
    }
  }

  void drive_write64(Write64Port& port,
                     CData& awready,
                     CData& wready,
                     CData& bvalid,
                     CData& bid,
                     CData& bresp,
                     CData& arready,
                     CData& rvalid,
                     QData& rdata,
                     CData& rid,
                     CData& rlast,
                     CData& rresp) {
    awready = !port.aw_pending && !port.bvalid;
    wready = !port.w_pending && !port.bvalid;
    bvalid = port.bvalid;
    bid = 0;
    bresp = 0;
    arready = 1;
    rvalid = 0;
    rdata = 0;
    rid = 0;
    rlast = 1;
    rresp = 0;
  }

  void update_write64(Write64Port& port,
                      CData awvalid,
                      uint64_t awaddr,
                      CData wvalid,
                      uint64_t wdata,
                      CData bready) {
    const bool aw_fire = awvalid && !port.aw_pending && !port.bvalid;
    const bool w_fire = wvalid && !port.w_pending && !port.bvalid;
    const bool b_fire = port.bvalid && bready;
    if (b_fire) {
      port.bvalid = false;
    }
    if (aw_fire) {
      port.awaddr = awaddr;
      port.aw_pending = true;
    }
    if (w_fire) {
      port.wdata = wdata;
      port.w_pending = true;
    }
    if (port.aw_pending && port.w_pending && !port.bvalid) {
      const size_t index = static_cast<size_t>(port.awaddr >> 3);
      if (index >= port.mem.size()) {
        port.mem.resize(index + 1, 0);
      }
      port.mem[index] = port.wdata;
      port.aw_pending = false;
      port.w_pending = false;
      port.bvalid = true;
    }
  }

  void drive_m_axi() {
    drive_read32(ptr,
                 top.m_axi_SpElement_list_ptr_ARREADY,
                 top.m_axi_SpElement_list_ptr_RVALID,
                 top.m_axi_SpElement_list_ptr_RDATA,
                 top.m_axi_SpElement_list_ptr_RID,
                 top.m_axi_SpElement_list_ptr_RLAST,
                 top.m_axi_SpElement_list_ptr_RRESP,
                 top.m_axi_SpElement_list_ptr_AWREADY,
                 top.m_axi_SpElement_list_ptr_WREADY,
                 top.m_axi_SpElement_list_ptr_BVALID,
                 top.m_axi_SpElement_list_ptr_BID,
                 top.m_axi_SpElement_list_ptr_BRESP);

#define DRIVE_MATRIX(ID)                                                       \
    drive_read512(matrix[ID],                                                   \
                  top.m_axi_Matrix_data_##ID##_ARREADY,                        \
                  top.m_axi_Matrix_data_##ID##_RVALID,                         \
                  top.m_axi_Matrix_data_##ID##_RDATA,                          \
                  top.m_axi_Matrix_data_##ID##_RID,                            \
                  top.m_axi_Matrix_data_##ID##_RLAST,                          \
                  top.m_axi_Matrix_data_##ID##_RRESP,                          \
                  top.m_axi_Matrix_data_##ID##_AWREADY,                        \
                  top.m_axi_Matrix_data_##ID##_WREADY,                         \
                  top.m_axi_Matrix_data_##ID##_BVALID,                         \
                  top.m_axi_Matrix_data_##ID##_BID,                            \
                  top.m_axi_Matrix_data_##ID##_BRESP)
    DRIVE_MATRIX(0);
    DRIVE_MATRIX(1);
    DRIVE_MATRIX(2);
    DRIVE_MATRIX(3);
    DRIVE_MATRIX(4);
    DRIVE_MATRIX(5);
    DRIVE_MATRIX(6);
    DRIVE_MATRIX(7);
#undef DRIVE_MATRIX

    drive_read512(x,
                  top.m_axi_X_ARREADY,
                  top.m_axi_X_RVALID,
                  top.m_axi_X_RDATA,
                  top.m_axi_X_RID,
                  top.m_axi_X_RLAST,
                  top.m_axi_X_RRESP,
                  top.m_axi_X_AWREADY,
                  top.m_axi_X_WREADY,
                  top.m_axi_X_BVALID,
                  top.m_axi_X_BID,
                  top.m_axi_X_BRESP);

    drive_write32(y,
                  top.m_axi_Y_out_AWREADY,
                  top.m_axi_Y_out_WREADY,
                  top.m_axi_Y_out_BVALID,
                  top.m_axi_Y_out_BID,
                  top.m_axi_Y_out_BRESP,
                  top.m_axi_Y_out_ARREADY,
                  top.m_axi_Y_out_RVALID,
                  top.m_axi_Y_out_RDATA,
                  top.m_axi_Y_out_RID,
                  top.m_axi_Y_out_RLAST,
                  top.m_axi_Y_out_RRESP);

    drive_write32(status,
                  top.m_axi_Status_AWREADY,
                  top.m_axi_Status_WREADY,
                  top.m_axi_Status_BVALID,
                  top.m_axi_Status_BID,
                  top.m_axi_Status_BRESP,
                  top.m_axi_Status_ARREADY,
                  top.m_axi_Status_RVALID,
                  top.m_axi_Status_RDATA,
                  top.m_axi_Status_RID,
                  top.m_axi_Status_RLAST,
                  top.m_axi_Status_RRESP);

    drive_write64(metrics,
                  top.m_axi_Metrics_AWREADY,
                  top.m_axi_Metrics_WREADY,
                  top.m_axi_Metrics_BVALID,
                  top.m_axi_Metrics_BID,
                  top.m_axi_Metrics_BRESP,
                  top.m_axi_Metrics_ARREADY,
                  top.m_axi_Metrics_RVALID,
                  top.m_axi_Metrics_RDATA,
                  top.m_axi_Metrics_RID,
                  top.m_axi_Metrics_RLAST,
                  top.m_axi_Metrics_RRESP);
  }

  AxiSample sample_m_axi() const {
    AxiSample sample;
    sample.ptr_arvalid = top.m_axi_SpElement_list_ptr_ARVALID;
    sample.ptr_araddr = top.m_axi_SpElement_list_ptr_ARADDR;
    sample.ptr_rready = top.m_axi_SpElement_list_ptr_RREADY;

#define SAMPLE_MATRIX(ID)                                                      \
    sample.matrix_arvalid[ID] = top.m_axi_Matrix_data_##ID##_ARVALID;          \
    sample.matrix_araddr[ID] = top.m_axi_Matrix_data_##ID##_ARADDR;            \
    sample.matrix_rready[ID] = top.m_axi_Matrix_data_##ID##_RREADY
    SAMPLE_MATRIX(0);
    SAMPLE_MATRIX(1);
    SAMPLE_MATRIX(2);
    SAMPLE_MATRIX(3);
    SAMPLE_MATRIX(4);
    SAMPLE_MATRIX(5);
    SAMPLE_MATRIX(6);
    SAMPLE_MATRIX(7);
#undef SAMPLE_MATRIX

    sample.x_arvalid = top.m_axi_X_ARVALID;
    sample.x_araddr = top.m_axi_X_ARADDR;
    sample.x_rready = top.m_axi_X_RREADY;

    sample.y_awvalid = top.m_axi_Y_out_AWVALID;
    sample.y_awaddr = top.m_axi_Y_out_AWADDR;
    sample.y_wvalid = top.m_axi_Y_out_WVALID;
    sample.y_wdata = top.m_axi_Y_out_WDATA;
    sample.y_bready = top.m_axi_Y_out_BREADY;

    sample.status_awvalid = top.m_axi_Status_AWVALID;
    sample.status_awaddr = top.m_axi_Status_AWADDR;
    sample.status_wvalid = top.m_axi_Status_WVALID;
    sample.status_wdata = top.m_axi_Status_WDATA;
    sample.status_bready = top.m_axi_Status_BREADY;

    sample.metrics_awvalid = top.m_axi_Metrics_AWVALID;
    sample.metrics_awaddr = top.m_axi_Metrics_AWADDR;
    sample.metrics_wvalid = top.m_axi_Metrics_WVALID;
    sample.metrics_wdata = top.m_axi_Metrics_WDATA;
    sample.metrics_bready = top.m_axi_Metrics_BREADY;
    return sample;
  }

  void update_m_axi(const AxiSample& sample) {
    update_read32(ptr,
                  sample.ptr_arvalid,
                  sample.ptr_araddr,
                  sample.ptr_rready);

#define UPDATE_MATRIX(ID)                                                       \
    update_read512(matrix[ID],                                                  \
                   sample.matrix_arvalid[ID],                                  \
                   sample.matrix_araddr[ID],                                   \
                   sample.matrix_rready[ID])
    UPDATE_MATRIX(0);
    UPDATE_MATRIX(1);
    UPDATE_MATRIX(2);
    UPDATE_MATRIX(3);
    UPDATE_MATRIX(4);
    UPDATE_MATRIX(5);
    UPDATE_MATRIX(6);
    UPDATE_MATRIX(7);
#undef UPDATE_MATRIX

    update_read512(x, sample.x_arvalid, sample.x_araddr, sample.x_rready);
    update_write32(y,
                   sample.y_awvalid,
                   sample.y_awaddr,
                   sample.y_wvalid,
                   sample.y_wdata,
                   sample.y_bready);
    update_write32(status,
                   sample.status_awvalid,
                   sample.status_awaddr,
                   sample.status_wvalid,
                   sample.status_wdata,
                   sample.status_bready);
    update_write64(metrics,
                   sample.metrics_awvalid,
                   sample.metrics_awaddr,
                   sample.metrics_wvalid,
                   sample.metrics_wdata,
                   sample.metrics_bready);
  }

  void cycle() {
    drive_m_axi();
    top.ap_clk = 0;
    top.eval();
    const AxiSample sample = sample_m_axi();
    context.timeInc(5);
    drive_m_axi();
    top.ap_clk = 1;
    top.eval();
    update_m_axi(sample);
    context.timeInc(5);
  }

  void reset() {
    top.s_axi_control_AWVALID = 0;
    top.s_axi_control_WVALID = 0;
    top.s_axi_control_BREADY = 0;
    top.s_axi_control_ARVALID = 0;
    top.s_axi_control_RREADY = 0;
    top.s_axi_control_AWADDR = 0;
    top.s_axi_control_WDATA = 0;
    top.s_axi_control_WSTRB = 0xf;
    top.s_axi_control_ARADDR = 0;
    for (int i = 0; i < 8; ++i) {
      top.ap_rst_n = 0;
      cycle();
    }
    top.ap_rst_n = 1;
    for (int i = 0; i < 4; ++i) {
      cycle();
    }
  }

  void write_reg(uint32_t addr, uint32_t data) {
    top.s_axi_control_AWVALID = 1;
    top.s_axi_control_AWADDR = addr;
    top.s_axi_control_WVALID = 1;
    top.s_axi_control_WDATA = data;
    top.s_axi_control_WSTRB = 0xf;
    top.s_axi_control_BREADY = 1;
    top.s_axi_control_ARVALID = 0;
    top.s_axi_control_RREADY = 0;
    cycle();
    top.s_axi_control_AWVALID = 0;
    top.s_axi_control_WVALID = 0;
    for (int i = 0; i < 4; ++i) {
      top.s_axi_control_BREADY = 1;
      cycle();
    }
    top.s_axi_control_BREADY = 0;
    cycle();
  }

  void set_ptr_arg(int index, uint64_t value) {
    const uint32_t base = 0x10U + static_cast<uint32_t>(index) * 0x0cU;
    write_reg(base, static_cast<uint32_t>(value));
    write_reg(base + 4U, static_cast<uint32_t>(value >> 32));
  }

  void launch() {
    for (int arg = 0; arg < 13; ++arg) {
      set_ptr_arg(arg, 0);
    }
    write_reg(0x0ac, 1);   // Batch_num
    write_reg(0x0b4, 1);   // Matrix_len
    write_reg(0x0bc, 16);  // Row_num
    write_reg(0x0c4, 16);  // Column_num
    write_reg(0x0cc, 1);   // Iteration_num
    write_reg(0x000, 1);   // ap_start
  }
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

extern "C" int cuper_verilator_fmul32(int a, int b) {
  return static_cast<int>(
      float_bits(bits_float(static_cast<uint32_t>(a)) *
                 bits_float(static_cast<uint32_t>(b))));
}

extern "C" int cuper_verilator_fadd32(int a, int b) {
  return static_cast<int>(
      float_bits(bits_float(static_cast<uint32_t>(a)) +
                 bits_float(static_cast<uint32_t>(b))));
}

int main(int argc, char** argv) {
  try {
    VerilatedContext context;
    context.commandArgs(argc, argv);

    Sim sim;
    sim.reset();
    sim.launch();

    bool done = false;
    for (int cycle = 0; cycle < 200000; ++cycle) {
      sim.top.s_axi_control_AWVALID = 0;
      sim.top.s_axi_control_WVALID = 0;
      sim.top.s_axi_control_BREADY = 0;
      sim.top.s_axi_control_ARVALID = 0;
      sim.top.s_axi_control_RREADY = 0;
      sim.cycle();
      if (sim.top.__SYM__interrupt) {
        done = true;
        break;
      }
    }

    if (!done) {
      const auto* root = sim.top.rootp;
      std::cerr << "[axi-top-timeout]"
                << " state=" << static_cast<int>(root->CuperSpmvChisel8__DOT__state)
                << " lenIndex=" << static_cast<int>(root->CuperSpmvChisel8__DOT__lenIndex)
                << " ptrState=" << static_cast<int>(root->CuperSpmvChisel8__DOT__ptrState)
                << " xState=" << static_cast<int>(root->CuperSpmvChisel8__DOT__xState)
                << " writerState=" << static_cast<int>(root->CuperSpmvChisel8__DOT__writerState)
                << " datapathState=" << static_cast<int>(root->CuperSpmvChisel8__DOT__datapath__DOT__state)
                << " ptrBoundaryIndex=" << root->CuperSpmvChisel8__DOT__ptrBoundaryIndex
                << " xPacketIndex=" << root->CuperSpmvChisel8__DOT__xPacketIndex
                << " datapathDoneSeen=" << static_cast<int>(root->CuperSpmvChisel8__DOT__datapathDoneSeen)
                << " startPending=" << static_cast<int>(root->CuperSpmvChisel8__DOT__startPending)
                << " doneSticky=" << static_cast<int>(root->CuperSpmvChisel8__DOT__doneSticky)
                << " ptrARVALID=" << static_cast<int>(sim.top.m_axi_SpElement_list_ptr_ARVALID)
                << " ptrARREADY=" << static_cast<int>(sim.top.m_axi_SpElement_list_ptr_ARREADY)
                << " ptrARADDR=0x" << std::hex << sim.top.m_axi_SpElement_list_ptr_ARADDR << std::dec
                << " ptrRVALID=" << static_cast<int>(sim.top.m_axi_SpElement_list_ptr_RVALID)
                << " ptrRREADY=" << static_cast<int>(sim.top.m_axi_SpElement_list_ptr_RREADY)
                << " ptrModelRvalid=" << static_cast<int>(sim.ptr.rvalid)
                << "\n";
    }
    require(done, "CuperSpmvChisel8 top timed out before interrupt");
    require(sim.status.mem[0] == 1, "Status[0] not complete");
    require(sim.status.mem[1] == kStatusMagic, "Status magic mismatch");
    require(sim.status.mem[11] == 0xff, "matrix done mask mismatch");
    require(sim.status.mem[12] == 0, "read error mask nonzero");
    require(sim.status.mem[13] == 0, "write error mask nonzero");
    require(sim.status.mem[31] == kSpmvMagic, "SpMV magic mismatch");
    require(sim.status.mem[32] == 8, "tagged-pair expected count mismatch");
    require(sim.status.mem[33] == 8, "tagged-pair read count mismatch");
    require(sim.status.mem[34] == 16, "scalar-write expected count mismatch");
    require(sim.status.mem[35] == 16, "scalar-write response count mismatch");
    require(sim.metrics.mem[0] == kMetricsMagic, "Metrics magic mismatch");
#ifndef CUPER_SPMV_CHISEL8_SLIM_DEBUG
    require(sim.metrics.mem[47] >= 1, "debug valid slot count is zero");
    require(sim.metrics.mem[50] >= 1, "debug nonzero product count is zero");
    require(sim.metrics.mem[53] >= 1, "debug nonzero tagged count is zero");
    require(sim.status.mem[56] >= 1, "debug core nonzero output count is zero");
    require(sim.status.mem[57] >= 1, "debug fadd nonzero output count is zero");
    require(sim.status.mem[58] >= 1, "debug partial nonzero read count is zero");
    require((sim.metrics.mem[62] & 0xffffffffULL) == sim.status.mem[56],
            "packed core nonzero output count mismatch");
    require((sim.metrics.mem[62] >> 32) == sim.status.mem[57],
            "packed fadd nonzero output count mismatch");
    require((sim.metrics.mem[63] >> 32) == sim.status.mem[58],
            "packed partial nonzero read count mismatch");
#else
    require(sim.metrics.mem[47] == 0, "slim debug valid slot count is not zero");
    require(sim.status.mem[56] == 0, "slim debug core count is not zero");
    require(sim.status.mem[57] == 0, "slim debug fadd count is not zero");
    require(sim.status.mem[58] == 0, "slim debug partial count is not zero");
#endif
    require(sim.status.mem[45] >= 1, "nonzero scalar write count is zero");
    require(sim.y.mem[0] == float_bits(2.0f), "Y[0] mismatch");

    std::cout << "CuperSpmvChisel8 AXI top smoke PASS"
              << " y0=" << bits_float(sim.y.mem[0])
#ifdef CUPER_SPMV_CHISEL8_SLIM_DEBUG
              << " slim_debug=1"
#endif
              << " valid_slots=" << sim.metrics.mem[47]
              << " nonzero_products=" << sim.metrics.mem[50]
              << " core_nonzero_out=" << sim.status.mem[56]
              << " fadd_nonzero_out=" << sim.status.mem[57]
              << " partial_read_nonzero=" << sim.status.mem[58]
              << " nonzero_y_writes=" << sim.status.mem[45]
              << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "CuperSpmvChisel8 AXI top smoke FAIL: " << error.what() << "\n";
    return 1;
  }
}
