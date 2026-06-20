#include "Vooo_accumulator.h"

#include "verilated.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Event {
  bool last = false;
  uint32_t row = 0;
  int32_t value = 0;
};

struct Expected {
  uint32_t row = 0;
  int32_t value = 0;
};

struct Args {
  std::string input_path = "build/csr_input.tsv";
  std::string expected_path = "build/csr_expected.tsv";
  uint64_t timeout_cycles = 100000000ULL;
  int source_pause_period = 0;
  int sink_pause_period = 0;
  int hbm_channels = 16;
  int entries = 8;
  uint32_t row_count = 0;
  double freq_mhz = 150.0;
};

struct BucketStats {
  uint64_t events = 0;
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t evicts = 0;
  uint64_t estimated_cycles = 0;
  std::vector<uint32_t> cam_rows;
  std::vector<uint8_t> cam_valid;
  size_t victim = 0;
};

[[noreturn]] void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --input PATH --expected PATH [--timeout-cycles N]"
               " [--source-pause-period N] [--sink-pause-period N]"
               " [--hbm-channels N] [--entries N] [--row-count N]"
               " [--freq-mhz F]\n";
  std::exit(2);
}

uint64_t parse_u64(const char* name, const char* text) {
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, 0);
  if (end == text || *end != '\0') {
    std::cerr << "invalid " << name << ": " << text << "\n";
    std::exit(2);
  }
  return static_cast<uint64_t>(parsed);
}

int parse_i32(const char* name, const char* text) {
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, 0);
  if (end == text || *end != '\0') {
    std::cerr << "invalid " << name << ": " << text << "\n";
    std::exit(2);
  }
  return static_cast<int>(parsed);
}

double parse_double(const char* name, const char* text) {
  char* end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (end == text || *end != '\0' || parsed <= 0.0) {
    std::cerr << "invalid " << name << ": " << text << "\n";
    std::exit(2);
  }
  return parsed;
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) {
      args.input_path = argv[++i];
    } else if (arg == "--expected" && i + 1 < argc) {
      args.expected_path = argv[++i];
    } else if (arg == "--timeout-cycles" && i + 1 < argc) {
      args.timeout_cycles = parse_u64("--timeout-cycles", argv[++i]);
    } else if (arg == "--source-pause-period" && i + 1 < argc) {
      args.source_pause_period = parse_i32("--source-pause-period", argv[++i]);
    } else if (arg == "--sink-pause-period" && i + 1 < argc) {
      args.sink_pause_period = parse_i32("--sink-pause-period", argv[++i]);
    } else if (arg == "--hbm-channels" && i + 1 < argc) {
      args.hbm_channels = parse_i32("--hbm-channels", argv[++i]);
    } else if (arg == "--entries" && i + 1 < argc) {
      args.entries = parse_i32("--entries", argv[++i]);
    } else if (arg == "--row-count" && i + 1 < argc) {
      args.row_count = static_cast<uint32_t>(parse_u64("--row-count", argv[++i]));
    } else if (arg == "--freq-mhz" && i + 1 < argc) {
      args.freq_mhz = parse_double("--freq-mhz", argv[++i]);
    } else {
      usage(argv[0]);
    }
  }
  return args;
}

std::vector<Event> read_events(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "failed to open " << path << "\n";
    std::exit(1);
  }

  std::vector<Event> events;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    int last = 0;
    Event event;
    if (!(iss >> last >> event.row >> event.value)) {
      std::cerr << "bad input line: " << line << "\n";
      std::exit(1);
    }
    event.last = last != 0;
    events.push_back(event);
  }
  return events;
}

std::vector<Expected> read_expected(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "failed to open " << path << "\n";
    std::exit(1);
  }

  std::vector<Expected> expected;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    Expected item;
    std::istringstream iss(line);
    if (!(iss >> item.row >> item.value)) {
      std::cerr << "bad expected line: " << line << "\n";
      std::exit(1);
    }
    expected.push_back(item);
  }
  return expected;
}

bool paused(uint64_t cycle, int period) {
  return period > 0 && (cycle % static_cast<uint64_t>(period)) == 0;
}

void push_bucket_event(BucketStats& bucket, uint32_t row, int entries) {
  ++bucket.events;

  for (int i = 0; i < entries; ++i) {
    if (bucket.cam_valid[static_cast<size_t>(i)] &&
        bucket.cam_rows[static_cast<size_t>(i)] == row) {
      ++bucket.hits;
      return;
    }
  }

  ++bucket.misses;
  for (int i = 0; i < entries; ++i) {
    if (!bucket.cam_valid[static_cast<size_t>(i)]) {
      bucket.cam_valid[static_cast<size_t>(i)] = 1;
      bucket.cam_rows[static_cast<size_t>(i)] = row;
      return;
    }
  }

  ++bucket.evicts;
  bucket.cam_rows[bucket.victim] = row;
  bucket.victim = (bucket.victim + 1) % static_cast<size_t>(entries);
}

void print_parallel_accumulator_estimate(const std::vector<Event>& events,
                                         const Args& args) {
  if (args.hbm_channels <= 0 || args.entries <= 0) {
    return;
  }

  uint32_t inferred_rows = args.row_count;
  if (inferred_rows == 0) {
    for (const Event& event : events) {
      if (!event.last && event.row + 1 > inferred_rows) {
        inferred_rows = event.row + 1;
      }
    }
  }

  const int pair_lanes = 8;
  const int bucket_count = args.hbm_channels * pair_lanes;
  std::vector<BucketStats> buckets(static_cast<size_t>(bucket_count));
  for (BucketStats& bucket : buckets) {
    bucket.cam_rows.assign(static_cast<size_t>(args.entries), 0);
    bucket.cam_valid.assign(static_cast<size_t>(args.entries), 0);
  }

  for (const Event& event : events) {
    if (event.last) {
      continue;
    }
    const uint32_t packet_idx = event.row >> 4;
    const uint32_t owner = packet_idx % static_cast<uint32_t>(args.hbm_channels);
    const uint32_t pair_lane = (event.row & 15U) >> 1;
    const uint32_t bucket_id = owner * pair_lanes + pair_lane;
    push_bucket_event(buckets[static_cast<size_t>(bucket_id)],
                      event.row,
                      args.entries);
  }

  const uint64_t num_out_packets = (static_cast<uint64_t>(inferred_rows) + 15U) >> 4;
  const uint64_t owner_groups =
      (num_out_packets + static_cast<uint64_t>(args.hbm_channels) - 1U) /
      static_cast<uint64_t>(args.hbm_channels);
  const uint64_t finish_overhead = static_cast<uint64_t>(args.entries) + owner_groups + 16U;

  uint64_t total_events = 0;
  uint64_t active_buckets = 0;
  uint64_t max_events = 0;
  uint64_t max_evicts = 0;
  uint64_t max_cycles = 0;
  int max_event_bucket = 0;
  int max_cycle_bucket = 0;

  for (int i = 0; i < bucket_count; ++i) {
    BucketStats& bucket = buckets[static_cast<size_t>(i)];
    bucket.estimated_cycles = bucket.events + bucket.evicts + finish_overhead;
    total_events += bucket.events;
    if (bucket.events != 0) {
      ++active_buckets;
    }
    if (bucket.events > max_events) {
      max_events = bucket.events;
      max_event_bucket = i;
    }
    if (bucket.evicts > max_evicts) {
      max_evicts = bucket.evicts;
    }
    if (bucket.estimated_cycles > max_cycles) {
      max_cycles = bucket.estimated_cycles;
      max_cycle_bucket = i;
    }
  }

  const double avg_events =
      bucket_count == 0 ? 0.0
                        : static_cast<double>(total_events) /
                              static_cast<double>(bucket_count);
  const double balance =
      max_events == 0 ? 0.0 : avg_events / static_cast<double>(max_events);
  const double estimated_ms =
      static_cast<double>(max_cycles) / args.freq_mhz / 1000.0;

  const BucketStats& hot = buckets[static_cast<size_t>(max_cycle_bucket)];
  std::cout << "      parallel_accumulator_estimate:"
            << " hbm_channels=" << args.hbm_channels
            << " accumulators=" << bucket_count
            << " active=" << active_buckets
            << " owner_groups=" << owner_groups
            << " avg_events=" << avg_events
            << " max_events=" << max_events << "@bucket" << max_event_bucket
            << " max_evicts=" << max_evicts
            << " balance=" << balance << "\n";
  std::cout << "      hot_accumulator:"
            << " bucket=" << max_cycle_bucket
            << " events=" << hot.events
            << " hits=" << hot.hits
            << " misses=" << hot.misses
            << " evicts=" << hot.evicts
            << " estimated_one_pass_cycles=" << max_cycles
            << " estimated_ms_at_" << args.freq_mhz << "MHz=" << estimated_ms
            << "\n";
}

void eval_edge(Vooo_accumulator& top, VerilatedContext& context, int clk) {
  top.clk = clk;
  top.eval();
  context.timeInc(5);
}

}  // namespace

int main(int argc, char** argv) {
  VerilatedContext context;
  context.commandArgs(argc, argv);

  const Args args = parse_args(argc, argv);
  const std::vector<Event> events = read_events(args.input_path);
  const std::vector<Expected> expected = read_expected(args.expected_path);

  if (events.empty() || !events.back().last) {
    std::cerr << "input stream must end with a last event\n";
    return 1;
  }

  Vooo_accumulator top;
  uint64_t cycle = 0;
  size_t input_idx = 0;
  size_t expected_idx = 0;
  bool finished = false;

  while (!finished && cycle < args.timeout_cycles) {
    const bool rst = cycle < 5;
    const bool source_pause = paused(cycle, args.source_pause_period);
    const bool sink_pause = paused(cycle, args.sink_pause_period);

    top.rst = rst ? 1 : 0;
    top.out_ready = (!rst && !sink_pause) ? 1 : 0;

    if (!rst && input_idx < events.size() && !source_pause) {
      const Event& event = events[input_idx];
      top.in_valid = 1;
      top.in_last = event.last ? 1 : 0;
      top.in_row = event.row;
      top.in_value = static_cast<uint32_t>(event.value);
    } else {
      top.in_valid = 0;
      top.in_last = 0;
      top.in_row = 0;
      top.in_value = 0;
    }

    eval_edge(top, context, 0);

    const bool input_fire = !rst && top.in_valid && top.in_ready;
    const bool output_fire = !rst && top.out_valid && top.out_ready;

    if (output_fire) {
      if (top.out_last) {
        if (expected_idx != expected.size()) {
          std::cerr << "FAIL: output ended at expected_idx=" << expected_idx
                    << " expect_count=" << expected.size() << "\n";
          return 1;
        }
        finished = true;
      } else {
        if (expected_idx >= expected.size()) {
          std::cerr << "FAIL: extra output row=" << top.out_row
                    << " value=" << static_cast<int32_t>(top.out_value) << "\n";
          return 1;
        }
        const Expected& want = expected[expected_idx];
        const int32_t got_value = static_cast<int32_t>(top.out_value);
        if (top.out_row != want.row || got_value != want.value) {
          std::cerr << "FAIL: output[" << expected_idx << "] got row="
                    << top.out_row << " value=" << got_value
                    << ", expected row=" << want.row
                    << " value=" << want.value << "\n";
          return 1;
        }
        ++expected_idx;
      }
    }

    eval_edge(top, context, 1);

    if (input_fire) {
      ++input_idx;
    }
    ++cycle;
  }

  if (!finished) {
    std::cerr << "FAIL: timeout cycle=" << cycle << " input_idx=" << input_idx
              << " expected_idx=" << expected_idx << " busy="
              << static_cast<int>(top.busy) << "\n";
    std::cerr << "      in=" << top.dbg_in_count
              << " hit=" << top.dbg_hit_count
              << " miss=" << top.dbg_miss_count
              << " evict=" << top.dbg_evict_count
              << " stall=" << top.dbg_stall_count
              << " out=" << top.dbg_out_count << "\n";
    return 1;
  }

  const uint64_t contribution_events = events.size() - 1;
  const double cycles_per_event =
      contribution_events == 0
          ? 0.0
          : static_cast<double>(cycle) / static_cast<double>(contribution_events);
  const double estimated_ms = static_cast<double>(cycle) / args.freq_mhz / 1000.0;

  std::cout << "PASS: input_events=" << contribution_events
            << " expected_rows=" << expected.size()
            << " cycles=" << cycle
            << " cycles_per_event=" << cycles_per_event
            << " estimated_ms_at_" << args.freq_mhz << "MHz=" << estimated_ms
            << "\n";
  std::cout << "      in=" << top.dbg_in_count
            << " hit=" << top.dbg_hit_count
            << " miss=" << top.dbg_miss_count
            << " evict=" << top.dbg_evict_count
            << " stall=" << top.dbg_stall_count
            << " out=" << top.dbg_out_count << "\n";
  print_parallel_accumulator_estimate(events, args);

  top.final();
  return 0;
}
