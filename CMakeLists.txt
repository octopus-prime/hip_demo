cmake_minimum_required(VERSION 3.25)
project(hip_demo LANGUAGES C CXX)

unset(HIP_DEMO_BUILD_HIPRT CACHE)
unset(HIP_DEMO_HIPRT_MESH CACHE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(
    HIPCC_EXECUTABLE
    NAMES hipcc hipcc.bat
    HINTS
        "C:/Program Files/AMD/ROCm/7.1/bin"
        /opt/rocm-7.1/bin
        "$ENV{HIP_PATH}/bin"
        "$ENV{ROCM_PATH}/bin"
        /opt/rocm/bin
)
if(NOT HIPCC_EXECUTABLE)
    message(FATAL_ERROR
        "HIP SDK 7.1 hipcc was not found. Install HIP SDK 7.1 or set HIPCC_EXECUTABLE to its compiler.")
endif()

get_filename_component(HIPCC_BIN_DIR "${HIPCC_EXECUTABLE}" DIRECTORY)
get_filename_component(HIP_SDK_ROOT "${HIPCC_BIN_DIR}" DIRECTORY)
set(HIP_SDK_ENV "${CMAKE_COMMAND}" -E env "HIP_PATH=${HIP_SDK_ROOT}" "ROCM_PATH=${HIP_SDK_ROOT}")
execute_process(
    COMMAND ${HIP_SDK_ENV} "${HIPCC_EXECUTABLE}" --version
    RESULT_VARIABLE HIP_VERSION_RESULT
    OUTPUT_VARIABLE HIP_COMPILER_VERSION
    ERROR_VARIABLE HIP_VERSION_ERROR
)
if(NOT HIP_VERSION_RESULT STREQUAL "0" OR NOT HIP_COMPILER_VERSION MATCHES "HIP version: 7\\.1\\.")
    message(FATAL_ERROR
        "HIP SDK 7.1 is required; ${HIPCC_EXECUTABLE} reported:\n"
        "${HIP_COMPILER_VERSION}${HIP_VERSION_ERROR}\n"
        "Set HIPCC_EXECUTABLE to the HIP SDK 7.1 compiler or use a fresh build directory.")
endif()
set(HIP_ARCHITECTURE "" CACHE STRING "AMD GPU targets (semicolon-separated); empty detects local GPUs")
set(HIP_DEMO_SHARED_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_common.h")
set(HIPRT_SCENE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/hiprt_scene.h")

if(HIP_ARCHITECTURE)
    set(HIP_DEMO_ARCHITECTURES ${HIP_ARCHITECTURE})
else()
    unset(AMDGPU_ARCH_EXECUTABLE CACHE)
    find_program(AMDGPU_ARCH_EXECUTABLE NAMES amdgpu-arch HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
    if(NOT AMDGPU_ARCH_EXECUTABLE)
        message(FATAL_ERROR "amdgpu-arch was not found. Set HIP_ARCHITECTURE explicitly.")
    endif()
    execute_process(
        COMMAND ${HIP_SDK_ENV} "${AMDGPU_ARCH_EXECUTABLE}"
        RESULT_VARIABLE AMDGPU_ARCH_RESULT
        OUTPUT_VARIABLE AMDGPU_ARCH_OUTPUT
        ERROR_VARIABLE AMDGPU_ARCH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT AMDGPU_ARCH_RESULT STREQUAL "0" OR AMDGPU_ARCH_OUTPUT STREQUAL "")
        message(FATAL_ERROR
            "GPU architecture detection failed (${AMDGPU_ARCH_RESULT}): ${AMDGPU_ARCH_ERROR}\n"
            "Set HIP_ARCHITECTURE explicitly.")
    endif()
    string(REPLACE "\r\n" "\n" AMDGPU_ARCH_OUTPUT "${AMDGPU_ARCH_OUTPUT}")
    string(REPLACE "\n" ";" HIP_DEMO_ARCHITECTURES "${AMDGPU_ARCH_OUTPUT}")
endif()
list(REMOVE_DUPLICATES HIP_DEMO_ARCHITECTURES)

set(HIPCC_ARGUMENTS --std=c++23 -O3)
foreach(architecture IN LISTS HIP_DEMO_ARCHITECTURES)
    if(NOT architecture MATCHES "^gfx[0-9a-z]+(:[a-z0-9_+-]+)*$")
        message(FATAL_ERROR "Invalid HIP architecture: ${architecture}")
    endif()
    list(APPEND HIPCC_ARGUMENTS "--offload-arch=${architecture}")
endforeach()
message(STATUS "HIP GPU architectures: ${HIP_DEMO_ARCHITECTURES}")

file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hip_demo_architectures.h"
    CONTENT "#define HIP_DEMO_ARCHITECTURES \"@HIP_DEMO_ARCHITECTURES@\"\n"
    @ONLY
)
list(APPEND HIPCC_ARGUMENTS "-I${CMAKE_CURRENT_BINARY_DIR}")

execute_process(
    COMMAND ${HIP_SDK_ENV} "${HIPCC_EXECUTABLE}" -print-resource-dir
    OUTPUT_VARIABLE HIP_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE HIP_RESOURCE_DIR_RESULT
)
if(NOT HIP_RESOURCE_DIR_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not query the HIP compiler resource directory.")
endif()

set(HIP_RUNTIME_WRAPPER
    "${HIP_RESOURCE_DIR}/include/__clang_hip_runtime_wrapper.h"
)
if(EXISTS "${HIP_RUNTIME_WRAPPER}")
    file(READ "${HIP_RUNTIME_WRAPPER}" HIP_RUNTIME_WRAPPER_CONTENTS)
    string(FIND "${HIP_RUNTIME_WRAPPER_CONTENTS}" "#include <cmath>" HIP_CMATH_POSITION)
    string(FIND "${HIP_RUNTIME_WRAPPER_CONTENTS}"
        "#include <__clang_cuda_math_forward_declares.h>"
        HIP_FORWARD_DECLARATIONS_POSITION
    )
    if(HIP_CMATH_POSITION GREATER -1
        AND HIP_FORWARD_DECLARATIONS_POSITION GREATER HIP_CMATH_POSITION)
        set(HIP_COMPATIBILITY_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/hip-compat")
        file(MAKE_DIRECTORY "${HIP_COMPATIBILITY_INCLUDE_DIR}")
        string(REPLACE
            "#include <__clang_cuda_math_forward_declares.h>"
            ""
            HIP_RUNTIME_WRAPPER_CONTENTS
            "${HIP_RUNTIME_WRAPPER_CONTENTS}"
        )
        string(REPLACE
            "#include <cmath>"
            "#include <__clang_cuda_math_forward_declares.h>\n#include <cmath>"
            HIP_RUNTIME_WRAPPER_CONTENTS
            "${HIP_RUNTIME_WRAPPER_CONTENTS}"
        )
        file(WRITE
            "${HIP_COMPATIBILITY_INCLUDE_DIR}/__clang_hip_runtime_wrapper.h"
            "${HIP_RUNTIME_WRAPPER_CONTENTS}"
        )
        list(APPEND HIPCC_ARGUMENTS "-I${HIP_COMPATIBILITY_INCLUDE_DIR}")
    endif()
endif()

foreach(variable IN ITEMS HIPRT_INCLUDE_DIR HIPRT_LIBRARY HIPRT_RUNTIME HIPRT_DEVICE_LIBRARY
        HIPRT_KERNEL_BUNDLE HIPRT_OROCHI_BUNDLE HIPRT_HIP_RUNTIME HIPRT_COMGR_RUNTIME)
    unset(${variable} CACHE)
endforeach()
find_path(HIPRT_INCLUDE_DIR NAMES hiprt/hiprt.h HINTS "${HIP_SDK_ROOT}/include" NO_DEFAULT_PATH)
if(NOT HIPRT_INCLUDE_DIR)
    message(FATAL_ERROR "HIP RT headers were not found in ${HIP_SDK_ROOT}.")
endif()
file(STRINGS "${HIPRT_INCLUDE_DIR}/hiprt/hiprt.h" HIPRT_VERSION_LINE
    REGEX "^#define HIPRT_VERSION_STR ")
file(STRINGS "${HIPRT_INCLUDE_DIR}/hiprt/hiprt.h" HIP_VERSION_LINE
    REGEX "^#define HIP_VERSION_STR ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIPRT_VERSION "${HIPRT_VERSION_LINE}")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIPRT_HIP_VERSION "${HIP_VERSION_LINE}")
if(NOT HIPRT_VERSION STREQUAL "03000" OR HIPRT_HIP_VERSION STREQUAL "")
    message(FATAL_ERROR "HIP RT 3.0 from HIP SDK 7.1 is required (found ${HIPRT_VERSION}).")
endif()
find_library(HIPRT_LIBRARY NAMES "hiprt${HIPRT_VERSION}64"
    HINTS "${HIP_SDK_ROOT}/lib" "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
if(WIN32)
    set(HIPRT_BC_SUFFIX "win")
    find_file(HIPRT_RUNTIME NAMES "hiprt${HIPRT_VERSION}64.dll"
        HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
    find_file(HIPRT_HIP_RUNTIME NAMES amdhip64_7.dll HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
    find_file(HIPRT_COMGR_RUNTIME NAMES amd_comgr_3.dll HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
else()
    set(HIPRT_BC_SUFFIX "linux")
    set(HIPRT_RUNTIME "${HIPRT_LIBRARY}")
endif()
find_file(HIPRT_DEVICE_LIBRARY
    NAMES "hiprt${HIPRT_VERSION}_${HIPRT_HIP_VERSION}_amd_lib_${HIPRT_BC_SUFFIX}.bc"
    HINTS "${HIPCC_BIN_DIR}" "${HIP_SDK_ROOT}/lib" NO_DEFAULT_PATH)
find_file(HIPRT_KERNEL_BUNDLE
    NAMES "hiprt${HIPRT_VERSION}_${HIPRT_HIP_VERSION}_amd.hipfb"
    HINTS "${HIPCC_BIN_DIR}" "${HIP_SDK_ROOT}/lib" NO_DEFAULT_PATH)
find_file(HIPRT_OROCHI_BUNDLE NAMES "oro_compiled_kernels.hipfb"
    HINTS "${HIPCC_BIN_DIR}" "${HIP_SDK_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT HIPRT_LIBRARY OR NOT HIPRT_RUNTIME OR NOT HIPRT_DEVICE_LIBRARY
    OR NOT HIPRT_KERNEL_BUNDLE OR NOT HIPRT_OROCHI_BUNDLE
    OR (WIN32 AND (NOT HIPRT_HIP_RUNTIME OR NOT HIPRT_COMGR_RUNTIME)))
    message(FATAL_ERROR "HIP RT or required HIP SDK 7.1 runtime files were not found in ${HIP_SDK_ROOT}.")
endif()

set(HIPRT_LINK_OPTIONS)
if(UNIX)
    get_filename_component(HIPRT_LIBRARY_DIR "${HIPRT_LIBRARY}" DIRECTORY)
    list(APPEND HIPRT_LINK_OPTIONS "-Wl,-rpath,${HIPRT_LIBRARY_DIR}")
endif()
set(HIPRT_RUNTIME_FILES "${HIPRT_KERNEL_BUNDLE}" "${HIPRT_OROCHI_BUNDLE}")
if(WIN32)
    list(APPEND HIPRT_RUNTIME_FILES "${HIPRT_RUNTIME}" "${HIPRT_HIP_RUNTIME}" "${HIPRT_COMGR_RUNTIME}")
endif()
add_custom_target(
    hiprt_runtime
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${HIPRT_RUNTIME_FILES}
        "${CMAKE_CURRENT_BINARY_DIR}/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rply/LICENSE.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/RPLY-LICENSE.txt"
    DEPENDS ${HIPRT_RUNTIME_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rply/LICENSE.txt"
    VERBATIM
)
add_library(rply STATIC third_party/rply/rply.c)
target_include_directories(rply PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rply")
add_library(ply_mesh STATIC src/ply_mesh.cpp)
target_link_libraries(ply_mesh PRIVATE rply)
if(MSVC)
    set_property(TARGET rply ply_mesh PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

function(add_hiprt_executable target)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/src/${target}.hip")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${target}${CMAKE_EXECUTABLE_SUFFIX}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        COMMAND ${HIP_SDK_ENV} "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS}
            -fgpu-rdc "${source}"
            -x none "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}"
            "$<TARGET_FILE:ply_mesh>" "$<TARGET_FILE:rply>" ${HIPRT_LINK_OPTIONS}
            -o "${output}"
        DEPENDS "${source}" "${HIPRT_SCENE_SOURCE}" "${HIP_DEMO_SHARED_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ply_mesh.h" ply_mesh rply
            "${CMAKE_CURRENT_BINARY_DIR}/hip_demo_architectures.h"
            "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}"
        VERBATIM
        COMMENT "Compiling ${target}"
    )
    add_custom_target(${target} ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${output}"
            "${CMAKE_CURRENT_BINARY_DIR}/bin/${target}${CMAKE_EXECUTABLE_SUFFIX}"
        DEPENDS "${output}"
        VERBATIM)
    add_dependencies(${target} hiprt_runtime)
endfunction()

add_hiprt_executable(hiprt_demo)
add_hiprt_executable(hiprt_tests)
enable_testing()
add_test(NAME hiprt_tests
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_tests${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/triangle_ascii.ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/triangle_z_up.ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/orientation_z_up.ply")
set_tests_properties(hiprt_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
add_executable(ply_loader_tests tests/ply_loader_tests.cpp)
target_include_directories(ply_loader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ply_loader_tests PRIVATE ply_mesh)
if(MSVC)
    set_property(TARGET ply_loader_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
add_test(NAME ply_loader_tests COMMAND ply_loader_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_custom_target(
    run
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_demo${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_CURRENT_BINARY_DIR}/hiprt_demo.bmp"
    DEPENDS hiprt_demo
    USES_TERMINAL
    COMMENT "Rendering hiprt_demo.bmp with HIP RT"
)
