cmake_minimum_required(VERSION 3.25)
project(hip_demo LANGUAGES C CXX)

unset(HIP_DEMO_BUILD_HIPRT CACHE)
unset(HIP_DEMO_HIPRT_MESH CACHE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(Boost_ROOT "$ENV{USERPROFILE}\\Downloads\\boost_1_92_0" CACHE PATH "Boost root directory")
endif()
cmake_policy(PUSH)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost 1.92 REQUIRED)
cmake_policy(POP)

# Build the compiled component from the same Boost source distribution as the headers.
if(NOT EXISTS "${Boost_INCLUDE_DIR}/libs/program_options/src/cmdline.cpp")
    message(FATAL_ERROR "Boost.Program_options sources were not found. Set Boost_ROOT to a full Boost source distribution.")
endif()
set(BOOST_PROGRAM_OPTIONS_SOURCES
    cmdline config_file convert options_description parsers positional_options
    split utf8_codecvt_facet value_semantic variables_map winmain)
list(TRANSFORM BOOST_PROGRAM_OPTIONS_SOURCES PREPEND "${Boost_INCLUDE_DIR}/libs/program_options/src/")
list(TRANSFORM BOOST_PROGRAM_OPTIONS_SOURCES APPEND ".cpp")
add_library(boost_program_options STATIC ${BOOST_PROGRAM_OPTIONS_SOURCES})
add_library(Boost::program_options ALIAS boost_program_options)
target_link_libraries(boost_program_options PUBLIC Boost::headers)
target_compile_definitions(boost_program_options PUBLIC BOOST_PROGRAM_OPTIONS_NO_LIB
    BOOST_PROGRAM_OPTIONS_STATIC_LINK PRIVATE BOOST_PROGRAM_OPTIONS_SOURCE)

include(FetchContent)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG 99623ef7097352da7993ba614161fd6a311e046e
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(zlib)
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG cd952f49f95bb27154ae77dbb103032d95f6e580)
block(PROPAGATE libpng_SOURCE_DIR libpng_BINARY_DIR)
    set(SKIP_INSTALL_ALL ON)
    FetchContent_MakeAvailable(libpng)
endblock()
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG 3bd82b5f543bc84ccf2b1d0cdb63b95218099ee6)
FetchContent_MakeAvailable(freetype)
if(MSVC)
    set_property(TARGET freetype PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

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
file(GLOB_RECURSE HIP_DEMO_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

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
if(WIN32)
    # The SDK bitcode's host stub targets an older MSVC version, with the same Windows ABI.
    list(APPEND HIPCC_ARGUMENTS -Xarch_host -Wno-override-module)
endif()
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
list(APPEND HIPCC_ARGUMENTS "-I${CMAKE_CURRENT_BINARY_DIR}" "-I${CMAKE_CURRENT_SOURCE_DIR}/src")

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
configure_file(src2/assets.h.in hiprt_tutorial_assets.h @ONLY)

function(add_tutorial_executable target source_path)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${source_path}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${target}${CMAKE_EXECUTABLE_SUFFIX}")
    set(sources "${source}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/camera.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/context.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/geometry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/mesh.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/scene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src2/renderer.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        COMMAND ${HIP_SDK_ENV} "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS} -fgpu-rdc
            "-I${libpng_SOURCE_DIR}" "-I${libpng_BINARY_DIR}"
            ${sources}
            -x none "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}" ${HIPRT_LINK_OPTIONS}
            "$<TARGET_FILE:png_static>" "$<TARGET_FILE:zlibstatic>"
            "$<TARGET_FILE:ply_mesh>" "$<TARGET_FILE:rply>"
            -o "${output}"
        DEPENDS ${sources} ${HIP_DEMO_HEADERS}
            "${CMAKE_CURRENT_BINARY_DIR}/hiprt_tutorial_assets.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/camera.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/context.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/geometry.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/mesh.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/scene.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/renderer.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/dielectric.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/path_scattering.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/area_light.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/gpu_event.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/function_table.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/math/polynomial.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/math/torus_roots.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/device_buffer.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src2/error.h" "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}"
            png_static zlibstatic ply_mesh rply
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

add_tutorial_executable(hiprt_tutorial src2/main.cpp)
add_tutorial_executable(tutorial_renderer_tests tests/tutorial_renderer_tests.cpp)

add_library(rply STATIC third_party/rply/rply.c)
target_include_directories(rply PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rply")
add_library(ply_mesh STATIC src/components/geometry/mesh/loading.cpp)
target_include_directories(ply_mesh PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ply_mesh PRIVATE rply)
add_library(scene_description STATIC src/parsing/parser.cpp src/components/scene/builder.cpp)
target_include_directories(scene_description PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(scene_description PRIVATE Boost::headers)
target_compile_definitions(scene_description PRIVATE
    NOMINMAX
    HIP_DEMO_SCENE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scenes")
add_library(text_geometry STATIC src/components/geometry/text/outline.cpp)
target_include_directories(text_geometry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(text_geometry PRIVATE freetype)
add_library(app_support STATIC src/app/options.cpp src/saving/image.cpp src/saving/bmp.cpp src/saving/png.cpp)
target_include_directories(app_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(app_support PRIVATE Boost::program_options png_static)
add_custom_command(TARGET hiprt_runtime POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${freetype_SOURCE_DIR}/docs/FTL.TXT"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/FREETYPE-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${libpng_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/LIBPNG-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${zlib_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/ZLIB-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost_math/LICENSE_1_0.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/BOOST-MATH-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${Boost_INCLUDE_DIR}/LICENSE_1_0.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/bin/BOOST-PROGRAM-OPTIONS-LICENSE.txt"
    VERBATIM)
if(MSVC)
    target_compile_options(scene_description PRIVATE /bigobj)
    set_property(TARGET rply ply_mesh scene_description text_geometry app_support png_static zlibstatic boost_program_options
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

function(add_hiprt_executable target source_path)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${source_path}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${target}${CMAKE_EXECUTABLE_SUFFIX}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        COMMAND ${HIP_SDK_ENV} "${HIPCC_EXECUTABLE}" ${HIPCC_ARGUMENTS}
            -fgpu-rdc "${source}"
            -x none "${HIPRT_DEVICE_LIBRARY}" "${HIPRT_LIBRARY}"
            "$<TARGET_FILE:app_support>"
            "$<TARGET_FILE:boost_program_options>"
            "$<TARGET_FILE:ply_mesh>" "$<TARGET_FILE:rply>"
            "$<TARGET_FILE:scene_description>" ${HIPRT_LINK_OPTIONS}
            "$<TARGET_FILE:text_geometry>" "$<TARGET_FILE:freetype>"
            "$<TARGET_FILE:png_static>" "$<TARGET_FILE:zlibstatic>"
            -o "${output}"
        DEPENDS "${source}" ${HIP_DEMO_HEADERS}
            ply_mesh rply scene_description text_geometry freetype app_support png_static zlibstatic boost_program_options
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

add_hiprt_executable(hiprt_demo src/app/main.hip)
add_hiprt_executable(hiprt_tests tests/hiprt_tests.hip)
enable_testing()
add_test(NAME tutorial_renderer_tests
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/tutorial_renderer_tests${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_CURRENT_BINARY_DIR}/tutorial-renderer-tests.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME tutorial_image_tests COMMAND "${CMAKE_COMMAND}"
    "-DTUTORIAL=${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_tutorial${CMAKE_EXECUTABLE_SUFFIX}"
    "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/tutorial-image-tests"
    "-DPLY_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/orientation_z_up.ply"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tutorial_image_tests.cmake")
add_test(NAME hiprt_tests
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_tests${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/triangle_ascii.ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/triangle_z_up.ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/orientation_z_up.ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/small.hipscene")
set_tests_properties(hiprt_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
add_test(NAME render_progress_tests COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER=${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_demo${CMAKE_EXECUTABLE_SUFFIX}"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_progress_tests.cmake")
set_tests_properties(render_progress_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
add_executable(ply_loader_tests tests/ply_loader_tests.cpp)
target_include_directories(ply_loader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ply_loader_tests PRIVATE ply_mesh)
if(MSVC)
    set_property(TARGET ply_loader_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
add_test(NAME ply_loader_tests COMMAND ply_loader_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_executable(scene_description_tests tests/scene_description_tests.cpp)
target_include_directories(scene_description_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(scene_description_tests PRIVATE scene_description)
if(MSVC)
    set_property(TARGET scene_description_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
add_test(NAME scene_description_tests COMMAND scene_description_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/small.hipscene")
add_executable(app_support_tests tests/app_support_tests.cpp)
target_link_libraries(app_support_tests PRIVATE app_support Boost::headers png_static)
if(MSVC)
    set_property(TARGET app_support_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
add_test(NAME app_support_tests COMMAND app_support_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/small.hipscene"
    "${CMAKE_CURRENT_BINARY_DIR}/app-support-test.bmp")
add_executable(text_geometry_tests tests/text_geometry_tests.cpp)
target_include_directories(text_geometry_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(text_geometry_tests PRIVATE text_geometry)
if(MSVC)
    set_property(TARGET text_geometry_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
find_file(HIP_DEMO_TEST_FONT NAMES arial.ttf DejaVuSans.ttf
    PATHS "$ENV{WINDIR}/Fonts" /usr/share/fonts/truetype/dejavu)
if(HIP_DEMO_TEST_FONT)
    add_test(NAME text_geometry_tests COMMAND text_geometry_tests "${HIP_DEMO_TEST_FONT}")
    add_test(NAME hiprt_text_tests COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_tests${CMAKE_EXECUTABLE_SUFFIX}"
        --text "${HIP_DEMO_TEST_FONT}")
    set_tests_properties(hiprt_text_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
else()
    add_test(NAME text_geometry_tests COMMAND text_geometry_tests)
    message(STATUS "Set HIP_DEMO_TEST_FONT to enable font-loading and HIP RT text tests")
endif()
add_custom_target(
    run
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/hiprt_demo${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_CURRENT_BINARY_DIR}/hiprt_demo.bmp"
    DEPENDS hiprt_demo
    USES_TERMINAL
    COMMENT "Rendering hiprt_demo.bmp with HIP RT"
)
