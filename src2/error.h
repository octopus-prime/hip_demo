#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>

#include <stdexcept>
#include <string>

inline void check_hip(hipError_t result, const char* operation)
{
    if (result != hipSuccess) {
        throw std::runtime_error(std::string(operation) + ": " + hipGetErrorString(result));
    }
}

inline void check_hiprt(hiprtError result, const char* operation)
{
    if (result != hiprtSuccess) {
        throw std::runtime_error(std::string(operation) + ": HIP RT error " + std::to_string(result));
    }
}
