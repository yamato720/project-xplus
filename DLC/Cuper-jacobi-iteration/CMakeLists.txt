cmake_minimum_required(VERSION 3.16)
project(cuper_jacobi_iteration CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# 1. 环境与路径检测 (避免硬编码)
# -----------------------------------------------------------------------------
# 自动获取 Vitis HLS 路径，如果环境变量没设置，再回退到当前 U55C
# 部署记录使用的 2022.2 工具链。
if(NOT DEFINED ENV{XILINX_HLS})
    if(EXISTS "$ENV{HOME}/vivado/Vitis_HLS/2022.2/include")
        set(XILINX_HLS_PATH "$ENV{HOME}/vivado/Vitis_HLS/2022.2")
    else()
        set(XILINX_HLS_PATH "/tools/Xilinx2022/Vitis_HLS/2022.2")
    endif()
else()
    set(XILINX_HLS_PATH $ENV{XILINX_HLS})
endif()

# TAPA 的二进制安装包不提供 TAPAConfig.cmake，因此这里直接寻找
# 安装前缀。兼容新版 ~/.tapa 和旧版 ~/.rapidstream-tapa 布局。
set(TAPA_ROOT_CANDIDATES
    "${TAPA_ROOT}"
    "$ENV{TAPA_ROOT}"
    "$ENV{HOME}/.tapa/usr"
    "$ENV{HOME}/.rapidstream-tapa/usr"
)

foreach(candidate IN LISTS TAPA_ROOT_CANDIDATES)
    if(candidate AND EXISTS "${candidate}/include/tapa.h")
        set(TAPA_ROOT "${candidate}")
        break()
    endif()
endforeach()

if(NOT TAPA_ROOT)
    message(FATAL_ERROR "TAPA not found. Install it or set TAPA_ROOT to the TAPA usr prefix, for example $HOME/.tapa/usr")
endif()

set(TAPA_INCLUDE_DIRS "${TAPA_ROOT}/include")
set(TAPA_LIB_DIR "${TAPA_ROOT}/lib")
message(STATUS "Using TAPA_ROOT=${TAPA_ROOT}")
message(STATUS "Using XILINX_HLS_PATH=${XILINX_HLS_PATH}")

set(XRT_ROOT_CANDIDATES
    "$ENV{XILINX_XRT}"
    "${CMAKE_SOURCE_DIR}/../../../xilinx-local/opt/xilinx/xrt"
    "/opt/xilinx/xrt"
)

foreach(candidate IN LISTS XRT_ROOT_CANDIDATES)
    if(candidate AND EXISTS "${candidate}/include/experimental/xrt_bo.h")
        set(XILINX_XRT_PATH "${candidate}")
        break()
    endif()
endforeach()

if(NOT XILINX_XRT_PATH)
    message(FATAL_ERROR "XRT not found. Set XILINX_XRT to the XRT prefix.")
endif()
message(STATUS "Using XILINX_XRT_PATH=${XILINX_XRT_PATH}")

# -----------------------------------------------------------------------------
# 2. 头文件包含 (对齐新目录结构)
# -----------------------------------------------------------------------------
include_directories(
    ${TAPA_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include                # 你的共享头文件
    ${CMAKE_SOURCE_DIR}/src                    # 适配层源码
    ${XILINX_HLS_PATH}/include
    ${XILINX_HLS_PATH}/common/technology/autopilot
    ${XILINX_XRT_PATH}/include
)

# -----------------------------------------------------------------------------
# 3. 建立 Host 可执行文件
# -----------------------------------------------------------------------------
# 注意：在 TAPA 流程中，Cuper.cpp 通常包含硬件内核逻辑
# 如果是进行软件仿真 (Software Emulation)，需要把 Cuper.cpp 编进去
# 如果是编译真正的硬件 Host，通常只需要链接库，Cuper.cpp 由 tapa 另行处理
add_executable(cuper_jacobi_host
    host/main.cpp           # 移动到了 host/ 目录
    kernels/Cuper.cpp       # 移动到了 kernels/ 目录
)

set(JACOBI_HBM_CHANNELS_VALUE "$ENV{JACOBI_HBM_CHANNELS}")
if(JACOBI_HBM_CHANNELS_VALUE STREQUAL "" AND DEFINED ENV{JACOBI_WIDE_HBM} AND NOT "$ENV{JACOBI_WIDE_HBM}" STREQUAL "" AND NOT "$ENV{JACOBI_WIDE_HBM}" STREQUAL "0")
    set(JACOBI_HBM_CHANNELS_VALUE "24")
endif()
if(JACOBI_HBM_CHANNELS_VALUE STREQUAL "")
    set(JACOBI_HBM_CHANNELS_VALUE "16")
endif()

if(JACOBI_HBM_CHANNELS_VALUE STREQUAL "24")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_HBM_CHANNELS_24=1)
    message(STATUS "Cuper Jacobi HBM channels: 24")
elseif(JACOBI_HBM_CHANNELS_VALUE STREQUAL "32")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_HBM_CHANNELS_32=1)
    message(STATUS "Cuper Jacobi HBM channels: 32")
elseif(JACOBI_HBM_CHANNELS_VALUE STREQUAL "16")
    message(STATUS "Cuper Jacobi HBM channels: 16")
else()
    message(FATAL_ERROR "Unsupported JACOBI_HBM_CHANNELS=${JACOBI_HBM_CHANNELS_VALUE}; use 16, 24, or 32")
endif()

if(DEFINED ENV{JACOBI_DEADLOCK_DEBUG} AND NOT "$ENV{JACOBI_DEADLOCK_DEBUG}" STREQUAL "" AND NOT "$ENV{JACOBI_DEADLOCK_DEBUG}" STREQUAL "0")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_DEADLOCK_DEBUG=1)
    message(STATUS "Cuper Jacobi deadlock debug is ENABLED")
else()
    message(STATUS "Cuper Jacobi deadlock debug is disabled")
endif()

if(DEFINED ENV{JACOBI_TRACE_ISOTOPE} AND NOT "$ENV{JACOBI_TRACE_ISOTOPE}" STREQUAL "" AND NOT "$ENV{JACOBI_TRACE_ISOTOPE}" STREQUAL "0")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_TRACE_ISOTOPE=1)
    message(STATUS "Cuper Jacobi isotope trace is ENABLED")
else()
    message(STATUS "Cuper Jacobi isotope trace is disabled")
endif()

if(DEFINED ENV{JACOBI_TRACE_LIGHT} AND NOT "$ENV{JACOBI_TRACE_LIGHT}" STREQUAL "" AND NOT "$ENV{JACOBI_TRACE_LIGHT}" STREQUAL "0")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_TRACE_LIGHT=1)
    message(STATUS "Cuper Jacobi light trace is ENABLED")
else()
    message(STATUS "Cuper Jacobi light trace is disabled")
endif()

if(DEFINED ENV{JACOBI_WIDE_HBM} AND NOT "$ENV{JACOBI_WIDE_HBM}" STREQUAL "" AND NOT "$ENV{JACOBI_WIDE_HBM}" STREQUAL "0")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_WIDE_HBM=1)
    message(STATUS "Cuper Jacobi wide HBM is ENABLED")
else()
    message(STATUS "Cuper Jacobi wide HBM is disabled")
endif()

if(DEFINED ENV{JACOBI_BLOCKING_ENTRY_PROBE} AND NOT "$ENV{JACOBI_BLOCKING_ENTRY_PROBE}" STREQUAL "" AND NOT "$ENV{JACOBI_BLOCKING_ENTRY_PROBE}" STREQUAL "0")
    target_compile_definitions(cuper_jacobi_host PUBLIC JACOBI_BLOCKING_ENTRY_PROBE=1)
    message(STATUS "Cuper Jacobi blocking entry probe is ENABLED")
else()
    message(STATUS "Cuper Jacobi blocking entry probe is disabled")
endif()

# -----------------------------------------------------------------------------
# 4. 链接库 (保持你的手动链接逻辑，但改为变量管理)
# -----------------------------------------------------------------------------
# 建议：将这些库定义为一个变量，更清晰
set(TAPA_RUNTIME_LIBS
    ${TAPA_LIB_DIR}/libtapa.so
    ${TAPA_LIB_DIR}/libcontext.so
    ${TAPA_LIB_DIR}/libfrt.so
    ${TAPA_LIB_DIR}/libOpenCL.so
    ${TAPA_LIB_DIR}/libglog.so
    ${TAPA_LIB_DIR}/libgflags.so
    ${TAPA_LIB_DIR}/libthread.so
    ${TAPA_LIB_DIR}/libtinyxml2.so
    ${TAPA_LIB_DIR}/libyaml-cpp.so
)

target_link_libraries(cuper_jacobi_host
    PUBLIC
    ${TAPA_RUNTIME_LIBS}
    pthread
)

set_target_properties(cuper_jacobi_host PROPERTIES
    BUILD_RPATH "${TAPA_LIB_DIR};${XILINX_XRT_PATH}/lib"
)

add_executable(cuper_jacobi_mmap_probe_xrt
    host/mmap_probe_xrt.cpp
)

target_link_libraries(cuper_jacobi_mmap_probe_xrt
    PUBLIC
    ${XILINX_XRT_PATH}/lib/libxrt_coreutil.so
    uuid
    pthread
    rt
)

set_target_properties(cuper_jacobi_mmap_probe_xrt PROPERTIES
    BUILD_RPATH "${XILINX_XRT_PATH}/lib"
)
