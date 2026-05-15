cmake_minimum_required(VERSION 3.16)
project(cuper_new CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# 1. 环境与路径检测 (避免硬编码)
# -----------------------------------------------------------------------------
# 自动获取 Vitis HLS 路径，如果环境变量没设置，再回退到默认路径
if(NOT DEFINED ENV{XILINX_HLS})
    set(XILINX_HLS_PATH "/tools/Xilinx/Vitis_HLS/2023.1")
else()
    set(XILINX_HLS_PATH $ENV{XILINX_HLS})
endif()

# 找到 RapidStream-TAPA
find_package(TAPA REQUIRED)

# -----------------------------------------------------------------------------
# 2. 头文件包含 (对齐新目录结构)
# -----------------------------------------------------------------------------
include_directories(
    ${TAPA_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include                # 你的共享头文件
    ${CMAKE_SOURCE_DIR}/src                    # 适配层源码
    $ENV{HOME}/.rapidstream-tapa/usr/include 
    ${XILINX_HLS_PATH}/include
    ${XILINX_HLS_PATH}/common/technology/autopilot
)

# -----------------------------------------------------------------------------
# 3. 建立 Host 可执行文件
# -----------------------------------------------------------------------------
# 注意：在 TAPA 流程中，Cuper.cpp 通常包含硬件内核逻辑
# 如果是进行软件仿真 (Software Emulation)，需要把 Cuper.cpp 编进去
# 如果是编译真正的硬件 Host，通常只需要链接库，Cuper.cpp 由 tapa 另行处理
add_executable(cuper_host
    host/main.cpp           # 移动到了 host/ 目录
    kernels/Cuper.cpp       # 移动到了 kernels/ 目录
)

# -----------------------------------------------------------------------------
# 4. 链接库 (保持你的手动链接逻辑，但改为变量管理)
# -----------------------------------------------------------------------------
set(TAPA_LIB_DIR "$ENV{HOME}/.rapidstream-tapa/usr/lib")

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

target_link_libraries(cuper_host
    PUBLIC
    ${TAPA_RUNTIME_LIBS}
    pthread
)