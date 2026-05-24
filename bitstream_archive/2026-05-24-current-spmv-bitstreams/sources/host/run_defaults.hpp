#ifndef PROJECT_XPLUS_RUN_DEFAULTS_HPP
#define PROJECT_XPLUS_RUN_DEFAULTS_HPP

#include <cstdlib>
#include <filesystem>
#include <string>

namespace project_xplus::cgsolver::run_defaults {

namespace fs = std::filesystem;

inline constexpr double kTau = 1.0e-10;
inline constexpr int kMaxIters = 0;
inline constexpr unsigned int kDeviceIndex = 0;
inline constexpr bool kTiming = true;

inline fs::path project_root(const char* argv0) {
    fs::path exe_path = argv0 != nullptr ? fs::path(argv0) : fs::path();
    if (exe_path.empty()) {
        exe_path = fs::current_path() / "build" / "xplus_xrt_host";
    } else if (exe_path.is_relative()) {
        exe_path = fs::current_path() / exe_path;
    }

    exe_path = fs::weakly_canonical(exe_path);
    fs::path dir = exe_path.parent_path();
    if (dir.filename() == "build") {
        return dir.parent_path();
    }
    if (dir.parent_path().filename() == "build") {
        return dir.parent_path().parent_path();
    }
    return fs::current_path();
}

inline fs::path dataset_dir(const char* argv0) {
    return project_root(argv0) / "data" / "suitesparse" / "Schmid" / "csr" / "thermal2_n1024";
}

inline std::string xrt_target() {
    const char* emulation = std::getenv("XCL_EMULATION_MODE");
    if (emulation != nullptr) {
        const std::string mode = emulation;
        if (mode == "sw_emu" || mode == "hw_emu") {
            return mode;
        }
    }
    return "hw";
}

inline fs::path xclbin_path(const char* argv0) {
    return project_root(argv0) / "build" / xrt_target() / "cgsolver_jacobi_pcg.xclbin";
}

inline std::string report_prefix() {
    const std::string target = xrt_target();
    if (target == "sw_emu") {
        return "SW_thermal2_n1024";
    }
    if (target == "hw_emu") {
        return "HW_EMU_thermal2_n1024";
    }
    return "HW_thermal2_n1024";
}

inline fs::path report_json_path(const char* argv0) {
    return project_root(argv0) / "reports" / (report_prefix() + ".json");
}

inline fs::path report_text_path(const char* argv0) {
    return project_root(argv0) / "reports" / (report_prefix() + ".txt");
}

}  // namespace project_xplus::cgsolver::run_defaults

#endif
