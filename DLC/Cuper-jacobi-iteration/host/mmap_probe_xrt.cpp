#include "Cuper.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ert.h"
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

namespace {

constexpr int kProbeSlots = 64;

struct Options {
    std::filesystem::path xclbin_path;
    std::string kernel_name = "CuperJacobiMmapProbeOnly";
    int row_num = 16;
    int max_iters = 1;
    int column_num = 0;
    unsigned int device_index = 0;
    int sample_delay_ms = 100;
    int wait_timeout_ms = 5000;
};

int ParseInt(const char* text, const char* name) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

unsigned int ParseUint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

void Usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " <xclbin> [options]\n"
        << "Options:\n"
        << "  --kernel-name NAME       Default: CuperJacobiMmapProbeOnly\n"
        << "  --row-num N              Default: 16\n"
        << "  --max-iters N            Default: 1\n"
        << "  --column-num N           Default: row-num\n"
        << "  --device-index N         Default: 0\n"
        << "  --sample-delay-ms N      Sync BOs after this delay before wait, default 100\n"
        << "  --wait-timeout-ms N      run.wait timeout, default 5000\n";
}

Options ParseArgs(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        Usage(argv[0]);
        std::exit(0);
    }
    if (argc < 2) {
        Usage(argv[0]);
        throw std::runtime_error("missing xclbin path");
    }
    Options options;
    options.xclbin_path = std::filesystem::path(argv[1]);
    for (int index = 2; index < argc;) {
        const std::string arg = argv[index++];
        auto require_value = [&](const char* name) -> const char* {
            if (index >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[index++];
        };
        if (arg == "--kernel-name") {
            options.kernel_name = require_value("--kernel-name");
        } else if (arg == "--row-num") {
            options.row_num = ParseInt(require_value("--row-num"), "--row-num");
        } else if (arg == "--max-iters") {
            options.max_iters = ParseInt(require_value("--max-iters"), "--max-iters");
        } else if (arg == "--column-num") {
            options.column_num = ParseInt(require_value("--column-num"), "--column-num");
        } else if (arg == "--device-index") {
            options.device_index = ParseUint(require_value("--device-index"), "--device-index");
        } else if (arg == "--sample-delay-ms") {
            options.sample_delay_ms = ParseInt(require_value("--sample-delay-ms"), "--sample-delay-ms");
        } else if (arg == "--wait-timeout-ms") {
            options.wait_timeout_ms = ParseInt(require_value("--wait-timeout-ms"), "--wait-timeout-ms");
        } else if (arg == "--help" || arg == "-h") {
            Usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    if (options.column_num <= 0) {
        options.column_num = options.row_num;
    }
    return options;
}

const char* StateName(const ert_cmd_state state) {
    switch (state) {
        case ERT_CMD_STATE_NEW:
            return "NEW";
        case ERT_CMD_STATE_QUEUED:
            return "QUEUED";
        case ERT_CMD_STATE_RUNNING:
            return "RUNNING";
        case ERT_CMD_STATE_COMPLETED:
            return "COMPLETED";
        case ERT_CMD_STATE_ERROR:
            return "ERROR";
        case ERT_CMD_STATE_ABORT:
            return "ABORT";
        case ERT_CMD_STATE_SUBMITTED:
            return "SUBMITTED";
        case ERT_CMD_STATE_TIMEOUT:
            return "TIMEOUT";
        case ERT_CMD_STATE_NORESPONSE:
            return "NORESPONSE";
        case ERT_CMD_STATE_SKERROR:
            return "SKERROR";
        case ERT_CMD_STATE_SKCRASHED:
            return "SKCRASHED";
        default:
            return "UNKNOWN";
    }
}

template <typename T>
xrt::bo MakeBo(xrt::device& device,
               xrt::kernel& kernel,
               const int arg_index,
               const std::vector<T>& initial) {
    xrt::bo bo(device, initial.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(initial.begin(), initial.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, initial.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
void ReadBo(xrt::bo& bo, std::vector<T>& out) {
    bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, out.size() * sizeof(T), 0);
    const auto mapped = bo.map<T*>();
    std::copy(mapped, mapped + out.size(), out.begin());
}

void PrintInts(const std::string& label, const std::vector<int>& values, const int begin, const int end) {
    std::cout << label << "[" << begin << ".." << (end - 1) << "]=";
    for (int index = begin; index < end; ++index) {
        if (index != begin) {
            std::cout << ",";
        }
        std::cout << values[static_cast<std::size_t>(index)];
    }
    std::cout << "\n";
}

void PrintDoubles(const std::string& label,
                  const std::vector<double>& values,
                  const int begin,
                  const int end) {
    std::cout << label << "[" << begin << ".." << (end - 1) << "]=";
    for (int index = begin; index < end; ++index) {
        if (index != begin) {
            std::cout << ",";
        }
        std::cout << std::fixed << std::setprecision(0)
                  << values[static_cast<std::size_t>(index)];
    }
    std::cout << "\n";
}

void DumpSnapshot(const std::string& tag,
                  const std::vector<int>& status,
                  const std::vector<double>& metrics,
                  const std::vector<int>& debug) {
    std::cout << "[" << tag << "]\n";
    PrintInts("  Status", status, 0, 16);
    PrintDoubles("  Metrics", metrics, 0, 16);
    PrintInts("  Debug", debug, 0, 16);
    PrintInts("  Debug", debug, 48, 52);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = ParseArgs(argc, argv);
        std::cout << "[jacobi-mmap-probe-xrt] xclbin=" << options.xclbin_path
                  << " kernel=" << options.kernel_name
                  << " row_num=" << options.row_num
                  << " max_iters=" << options.max_iters
                  << " column_num=" << options.column_num
                  << " device_index=" << options.device_index << "\n";

        std::vector<int> status(kProbeSlots);
        std::vector<double> metrics(kProbeSlots);
        std::vector<int> debug(kProbeSlots);
        for (int index = 0; index < kProbeSlots; ++index) {
            status[static_cast<std::size_t>(index)] = 0x51510000 + index;
            metrics[static_cast<std::size_t>(index)] = -1000000.0 - static_cast<double>(index);
            debug[static_cast<std::size_t>(index)] = 0x53530000 + index;
        }

        xrt::device device(options.device_index);
        const auto uuid = device.load_xclbin(options.xclbin_path.string());
        xrt::kernel kernel(device, uuid, options.kernel_name.c_str());

        auto status_bo = MakeBo(device, kernel, 0, status);
        auto metrics_bo = MakeBo(device, kernel, 1, metrics);
        auto debug_bo = MakeBo(device, kernel, 2, debug);

        DumpSnapshot("before-launch-host-sentinel", status, metrics, debug);

        auto run = kernel(status_bo,
                          metrics_bo,
                          debug_bo,
                          options.row_num,
                          options.max_iters,
                          options.column_num);
        std::cout << "[jacobi-mmap-probe-xrt] launched\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(options.sample_delay_ms));
        ReadBo(status_bo, status);
        ReadBo(metrics_bo, metrics);
        ReadBo(debug_bo, debug);
        DumpSnapshot("after-sample-sync-before-wait", status, metrics, debug);

        const ert_cmd_state wait_state =
            run.wait(std::chrono::milliseconds(options.wait_timeout_ms));
        std::cout << "[jacobi-mmap-probe-xrt] wait_state=" << StateName(wait_state)
                  << "(" << static_cast<int>(wait_state) << ")\n";

        ReadBo(status_bo, status);
        ReadBo(metrics_bo, metrics);
        ReadBo(debug_bo, debug);
        DumpSnapshot("after-wait-sync", status, metrics, debug);

        if (wait_state == ERT_CMD_STATE_TIMEOUT) {
            const ert_cmd_state abort_state = run.abort();
            std::cout << "[jacobi-mmap-probe-xrt] abort_state=" << StateName(abort_state)
                      << "(" << static_cast<int>(abort_state) << ")\n";
            ReadBo(status_bo, status);
            ReadBo(metrics_bo, metrics);
            ReadBo(debug_bo, debug);
            DumpSnapshot("after-abort-sync", status, metrics, debug);
            return 124;
        }
        return (wait_state == ERT_CMD_STATE_COMPLETED) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[jacobi-mmap-probe-xrt] error: " << error.what() << "\n";
        return 1;
    }
}
