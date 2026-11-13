cmake_minimum_required(VERSION 3.25)
project(hip_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB ROCM_INSTALLATION_BIN_DIRS
    "C:/Program Files/AMD/ROCm/*/bin"
)

find_program(
    HIPCC_EXECUTABLE
    NAMES hipcc hipcc.bat
    HINTS
        "$ENV{HIP_PATH}/bin"
        "$ENV{ROCM_PATH}/bin"
        ${ROCM_INSTALLATION_BIN_DIRS}
        /opt/rocm/bin
)
if(NOT HIPCC_EXECUTABLE)
    message(FATAL_ERROR
        "hipcc was not found. Install AMD HIP SDK/ROCm and set HIP_PATH or ROCM_PATH.")
endif()

get_filename_component(HIPCC_BIN_DIR "${HIPCC_EXECUTABLE}" DIRECTORY)
get_filename_component(HIP_SDK_ROOT "${HIPCC_BIN_DIR}" DIRECTORY)
set(HIP_ARCHITECTURE "" CACHE STRING "AMD GPU targets (semicolon-separated); empty detects local GPUs")
set(HIP_DEMO_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/hip_demo${CMAKE_EXECUTABLE_SUFFIX}")
set(HIP_DEMO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/hip_demo.hip")
set(HIPRT_DEMO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/hiprt_demo.hip")
set(HIP_DEMO_SHARED_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_common.h")

if(HIP_ARCHITECTURE)
    set(HIP_DEMO_ARCHITECTURES ${HIP_ARCHITECTURE})
else()
    find_program(AMDGPU_ARCH_EXECUTABLE NAMES amdgpu-arch HINTS "${HIPCC_BIN_DIR}")
    if(NOT AMDGPU_ARCH_EXECUTABLE)
        message(FATAL_ERROR "amdgpu-arch was not found. Set HIP_ARCHITECTURE explicitly.")
    endif()
    execute_process(
        COMMAND "${AMDGPU_ARCH_EXECUTABLE}"
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
    COMMAND "${HIPCC_EXECUTABLE}" -print-resource-dir
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

add_custom_command(
    OUTPUT "${HIP_DEMO_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
    COMMAND "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS} "${HIP_DEMO_SOURCE}" -o "${HIP_DEMO_OUTPUT}"
    DEPENDS "${HIP_DEMO_SOURCE}" "${HIP_DEMO_SHARED_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/hip_demo_architectures.h"
    VERBATIM
    COMMENT "Compiling HIP C++23 ray tracer"
)

add_custom_target(hip_demo ALL DEPENDS "${HIP_DEMO_OUTPUT}")
add_custom_target(
    run
    COMMAND "${HIP_DEMO_OUTPUT}" "${CMAKE_CURRENT_BINARY_DIR}/hip_demo.bmp"
    DEPENDS hip_demo
    USES_TERMINAL
    COMMENT "Rendering hip_demo.bmp on the AMD GPU"
)

option(HIP_DEMO_BUILD_HIPRT "Build the HIP RT demo when HIP RT is installed" ON)
if(HIP_DEMO_BUILD_HIPRT)
    find_path(HIPRT_INCLUDE_DIR NAMES hiprt/hiprt.h HINTS "${HIP_SDK_ROOT}/include" NO_DEFAULT_PATH)
    if(HIPRT_INCLUDE_DIR)
        file(STRINGS "${HIPRT_INCLUDE_DIR}/hiprt/hiprt.h" HIPRT_VERSION_LINE
            REGEX "^#define HIPRT_VERSION_STR ")
        file(STRINGS "${HIPRT_INCLUDE_DIR}/hiprt/hiprt.h" HIP_VERSION_LINE
            REGEX "^#define HIP_VERSION_STR ")
        string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIPRT_VERSION "${HIPRT_VERSION_LINE}")
        string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIPRT_HIP_VERSION "${HIP_VERSION_LINE}")
        find_library(HIPRT_LIBRARY NAMES "hiprt${HIPRT_VERSION}64"
            HINTS "${HIP_SDK_ROOT}/lib" "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
        if(WIN32)
            set(HIPRT_BC_SUFFIX "win")
            find_file(HIPRT_RUNTIME NAMES "hiprt${HIPRT_VERSION}64.dll"
                HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
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
    endif()
    if(HIPRT_INCLUDE_DIR AND HIPRT_LIBRARY AND HIPRT_RUNTIME AND HIPRT_DEVICE_LIBRARY
        AND HIPRT_KERNEL_BUNDLE AND HIPRT_OROCHI_BUNDLE)
        if(WIN32 AND HIPRT_VERSION STREQUAL "03000")
            set(HIPRT_MESH_DEFAULT ON)
        else()
            set(HIPRT_MESH_DEFAULT OFF)
        endif()
        option(HIP_DEMO_HIPRT_MESH "Enable the HIP RT triangle mesh" ${HIPRT_MESH_DEFAULT})
        if(HIP_DEMO_HIPRT_MESH AND NOT HIPRT_VERSION STREQUAL "03000")
            message(FATAL_ERROR
                "The triangle mesh is verified with HIP RT 3.0 (HIP SDK 7.1). "
                "HIP RT ${HIPRT_VERSION} is not enabled for this scene. "
                "Use HIP SDK 7.1 or set HIP_DEMO_HIPRT_MESH=OFF.")
        endif()
        if(HIP_DEMO_HIPRT_MESH)
            set(HIPRT_MESH_CONFIG "#define HIP_DEMO_HIPRT_MESH 1\n")
            message(STATUS "HIP RT triangle mesh: enabled")
        else()
            set(HIPRT_MESH_CONFIG "/* HIP RT triangle mesh disabled */\n")
            message(STATUS "HIP RT triangle mesh: disabled (enable with HIP SDK 7.1 / HIP RT 3.0)")
        endif()
        file(CONFIGURE
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hiprt_mesh_config.h"
            CONTENT "${HIPRT_MESH_CONFIG}"
            @ONLY
        )
        set(HIPRT_DEMO_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_demo${CMAKE_EXECUTABLE_SUFFIX}")
        set(HIPRT_LINK_OPTIONS)
        if(UNIX)
            get_filename_component(HIPRT_LIBRARY_DIR "${HIPRT_LIBRARY}" DIRECTORY)
            list(APPEND HIPRT_LINK_OPTIONS "-Wl,-rpath,${HIPRT_LIBRARY_DIR}")
        endif()
        set(HIPRT_RUNTIME_FILES "${HIPRT_KERNEL_BUNDLE}" "${HIPRT_OROCHI_BUNDLE}")
        if(WIN32)
            list(APPEND HIPRT_RUNTIME_FILES "${HIPRT_RUNTIME}")
            if(HIP_DEMO_HIPRT_MESH)
                find_file(HIPRT_HIP_RUNTIME NAMES amdhip64_7.dll HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
                find_file(HIPRT_COMGR_RUNTIME NAMES amd_comgr_3.dll HINTS "${HIPCC_BIN_DIR}" NO_DEFAULT_PATH)
                if(NOT HIPRT_HIP_RUNTIME OR NOT HIPRT_COMGR_RUNTIME)
                    message(FATAL_ERROR "The HIP SDK's amdhip64_7.dll and amd_comgr_3.dll are required for the mesh build.")
                endif()
                list(APPEND HIPRT_RUNTIME_FILES "${HIPRT_HIP_RUNTIME}" "${HIPRT_COMGR_RUNTIME}")
            endif()
        endif()
        add_custom_target(
            hiprt_runtime
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${HIPRT_RUNTIME_FILES}
                "${CMAKE_CURRENT_BINARY_DIR}/bin"
            DEPENDS ${HIPRT_RUNTIME_FILES}
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${HIPRT_DEMO_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
            COMMAND "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS}
                -fgpu-rdc "${HIPRT_DEMO_SOURCE}"
                -x none "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}" ${HIPRT_LINK_OPTIONS}
                -o "${HIPRT_DEMO_OUTPUT}"
            DEPENDS "${HIPRT_DEMO_SOURCE}" "${HIP_DEMO_SHARED_SOURCE}"
                "${CMAKE_CURRENT_BINARY_DIR}/hip_demo_architectures.h"
                "${CMAKE_CURRENT_BINARY_DIR}/hiprt_mesh_config.h"
                "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}"
            VERBATIM
            COMMENT "Compiling HIP RT C++23 ray tracer"
        )
        add_custom_target(hiprt_demo ALL DEPENDS "${HIPRT_DEMO_OUTPUT}")
        add_dependencies(hiprt_demo hiprt_runtime)
        add_custom_target(
            run_hiprt
            COMMAND "${HIPRT_DEMO_OUTPUT}" "${CMAKE_CURRENT_BINARY_DIR}/hiprt_demo.bmp"
            DEPENDS hiprt_demo
            USES_TERMINAL
            COMMENT "Rendering hiprt_demo.bmp with HIP RT"
        )
    else()
        message(WARNING "HIP RT headers or libraries were not found in ${HIP_SDK_ROOT}; skipping hiprt_demo.")
    endif()
endif()
