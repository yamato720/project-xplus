cmake_minimum_required(VERSION 3.16)
project(cuper_callipepla_pcg CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{XILINX_HLS})
  if(EXISTS "$ENV{HOME}/vivado/Vitis_HLS/2022.2/include")
    set(XILINX_HLS_PATH "$ENV{HOME}/vivado/Vitis_HLS/2022.2")
  else()
    set(XILINX_HLS_PATH "/tools/Xilinx2022/Vitis_HLS/2022.2")
  endif()
else()
  set(XILINX_HLS_PATH $ENV{XILINX_HLS})
endif()

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
  message(FATAL_ERROR "TAPA not found. Set TAPA_ROOT to the TAPA usr prefix.")
endif()

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

add_executable(cuper_callipepla_pcg_host
  host/main.cpp
  kernels/Cuper.cpp
)

target_include_directories(cuper_callipepla_pcg_host PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/kernels
  ${CMAKE_SOURCE_DIR}/../../host
  ${CMAKE_SOURCE_DIR}/../../include
  ${CMAKE_SOURCE_DIR}/../../src
  ${TAPA_ROOT}/include
  ${XILINX_HLS_PATH}/include
  ${XILINX_HLS_PATH}/common/technology/autopilot
  ${XILINX_XRT_PATH}/include
)

if(DEFINED ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING} AND
   NOT "$ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING}" STREQUAL "" AND
   NOT "$ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING}" STREQUAL "0")
  target_compile_definitions(cuper_callipepla_pcg_host PUBLIC JACOBI_SPMV_STRIP_PADDING=1)
endif()

if(DEFINED ENV{CUPER_CALLIPEPLA_TRACE_LIGHT} AND
   NOT "$ENV{CUPER_CALLIPEPLA_TRACE_LIGHT}" STREQUAL "" AND
   NOT "$ENV{CUPER_CALLIPEPLA_TRACE_LIGHT}" STREQUAL "0")
  target_compile_definitions(cuper_callipepla_pcg_host PUBLIC CUPER_CALLIPEPLA_TRACE_LIGHT=1)
endif()

set(CUPER_CALLIPEPLA_PROBE_MODE "$ENV{CUPER_CALLIPEPLA_PROBE_MODE}")
if(NOT "${CUPER_CALLIPEPLA_PROBE_MODE}" STREQUAL "")
  if(DEFINED ENV{CUPER_CALLIPEPLA_TRACE_LIGHT} AND
     NOT "$ENV{CUPER_CALLIPEPLA_TRACE_LIGHT}" STREQUAL "" AND
     NOT "$ENV{CUPER_CALLIPEPLA_TRACE_LIGHT}" STREQUAL "0")
    message(FATAL_ERROR "CUPER_CALLIPEPLA_PROBE_MODE is mutually exclusive with CUPER_CALLIPEPLA_TRACE_LIGHT")
  endif()
  if("${CUPER_CALLIPEPLA_PROBE_MODE}" STREQUAL "entry")
    set(CUPER_CALLIPEPLA_PROBE_MODE_ID 1)
  elseif("${CUPER_CALLIPEPLA_PROBE_MODE}" STREQUAL "cmd_drain")
    set(CUPER_CALLIPEPLA_PROBE_MODE_ID 2)
  elseif("${CUPER_CALLIPEPLA_PROBE_MODE}" STREQUAL "loader_drain")
    set(CUPER_CALLIPEPLA_PROBE_MODE_ID 3)
  else()
    message(FATAL_ERROR "CUPER_CALLIPEPLA_PROBE_MODE must be empty, entry, cmd_drain, or loader_drain")
  endif()
  target_compile_definitions(cuper_callipepla_pcg_host PUBLIC
    CUPER_CALLIPEPLA_PROBE_MODE_ID=${CUPER_CALLIPEPLA_PROBE_MODE_ID})
endif()

if("${CUPER_CALLIPEPLA_PROBE_MODE}" STREQUAL "loader_drain" AND
   (NOT DEFINED ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING} OR
    "$ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING}" STREQUAL "" OR
    "$ENV{CUPER_CALLIPEPLA_SPMV_STRIP_PADDING}" STREQUAL "0"))
  message(FATAL_ERROR "CUPER_CALLIPEPLA_PROBE_MODE=loader_drain requires CUPER_CALLIPEPLA_SPMV_STRIP_PADDING=1")
endif()

set(CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL "$ENV{CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}")
if("${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "")
  set(CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL "1")
endif()
if("${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "1" OR
   "${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "ptr")
  set(CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL 1)
elseif("${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "2" OR
       "${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "vector")
  set(CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL 2)
elseif("${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "3" OR
       "${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "matrix")
  set(CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL 3)
elseif("${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "4" OR
       "${CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL}" STREQUAL "matrix_full")
  set(CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL 4)
else()
  message(FATAL_ERROR "CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL must be 1/ptr, 2/vector, 3/matrix, or 4/matrix_full")
endif()
target_compile_definitions(cuper_callipepla_pcg_host PUBLIC
  CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL=${CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL})

if(DEFINED ENV{CUPER_CALLIPEPLA_SPMV_ACC_WINDOW} AND
   NOT "$ENV{CUPER_CALLIPEPLA_SPMV_ACC_WINDOW}" STREQUAL "")
  if(NOT "$ENV{CUPER_CALLIPEPLA_SPMV_ACC_WINDOW}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "CUPER_CALLIPEPLA_SPMV_ACC_WINDOW must be a positive integer")
  endif()
  target_compile_definitions(cuper_callipepla_pcg_host PUBLIC
    JACOBI_SPMV_ACC_WINDOW=$ENV{CUPER_CALLIPEPLA_SPMV_ACC_WINDOW})
endif()

target_link_libraries(cuper_callipepla_pcg_host PRIVATE
  ${TAPA_ROOT}/lib/libtapa.so
  ${TAPA_ROOT}/lib/libcontext.so
  ${TAPA_ROOT}/lib/libfrt.so
  ${TAPA_ROOT}/lib/libOpenCL.so
  ${TAPA_ROOT}/lib/libglog.so
  ${TAPA_ROOT}/lib/libgflags.so
  ${TAPA_ROOT}/lib/libthread.so
  ${TAPA_ROOT}/lib/libtinyxml2.so
  ${TAPA_ROOT}/lib/libyaml-cpp.so
  pthread
  rt
  uuid
  ${XILINX_XRT_PATH}/lib/libxrt_coreutil.so
)

set_target_properties(cuper_callipepla_pcg_host PROPERTIES
  BUILD_RPATH "${TAPA_ROOT}/lib;${XILINX_XRT_PATH}/lib"
)
