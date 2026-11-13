cmake_minimum_required(VERSION 3.25)
project(hip_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(
    HIPCC_EXECUTABLE
    NAMES hipcc hipcc.bat
    HINTS
        "$ENV{HIP_PATH}/bin"
        "$ENV{ROCM_PATH}/bin"
        /opt/rocm/bin
)
if(NOT HIPCC_EXECUTABLE)
    message(FATAL_ERROR
        "hipcc was not found. Install AMD HIP SDK/ROCm and set HIP_PATH or ROCM_PATH.")
endif()

set(HIP_ARCHITECTURE "" CACHE STRING "Optional AMD GPU target, e.g. gfx1201 for Radeon RX 9070 XT")
set(HIP_DEMO_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/hip_demo${CMAKE_EXECUTABLE_SUFFIX}")
set(HIP_DEMO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.hip")

set(HIPCC_ARGUMENTS --std=c++23 -O3)
if(HIP_ARCHITECTURE)
    list(APPEND HIPCC_ARGUMENTS "--offload-arch=${HIP_ARCHITECTURE}")
endif()

add_custom_command(
    OUTPUT "${HIP_DEMO_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
    COMMAND "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS} "${HIP_DEMO_SOURCE}" -o "${HIP_DEMO_OUTPUT}"
    DEPENDS "${HIP_DEMO_SOURCE}"
    VERBATIM
    COMMENT "Compiling HIP C++23 ray tracer"
)

add_custom_target(hip_demo ALL DEPENDS "${HIP_DEMO_OUTPUT}")
add_custom_target(
    run
    COMMAND "${HIP_DEMO_OUTPUT}" "${CMAKE_CURRENT_BINARY_DIR}/hip_demo.ppm"
    DEPENDS hip_demo
    USES_TERMINAL
    COMMENT "Rendering hip_demo.ppm on the AMD GPU"
)
