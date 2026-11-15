#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hip_demo {

struct HipBufferDeleter {
    template <typename T>
    void operator()(T* buffer) const noexcept {
        if (buffer != nullptr) {
            const hipError_t result = hipFree(buffer);
            if (result != hipSuccess) {
                std::cerr << "Freeing GPU buffer: " << hipGetErrorString(result) << '\n';
            }
        }
    }
};

inline void check_hip(hipError_t result, std::string_view operation) {
    if (result != hipSuccess) {
        throw std::runtime_error(
            std::string(operation) + ": " + hipGetErrorString(result));
    }
}

struct HipEvent {
    hipEvent_t value = nullptr;

    HipEvent(const HipEvent&) = delete;
    HipEvent& operator=(const HipEvent&) = delete;

    HipEvent() {
        check_hip(hipEventCreate(&value), "Creating GPU timing event");
    }

    ~HipEvent() {
        if (value) {
            const hipError_t result = hipEventDestroy(value);
            if (result != hipSuccess) {
                std::cerr << "Destroying GPU timing event: " << hipGetErrorString(result) << '\n';
            }
        }
    }
};

inline void check_hiprt(hiprtError result, std::string_view operation) {
    if (result != hiprtSuccess) {
        throw std::runtime_error(std::string(operation) + ": HIP RT error " + std::to_string(result));
    }
}

template <typename T>
std::unique_ptr<T, HipBufferDeleter> allocate_device(std::size_t count, std::string_view operation) {
    T* pointer = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&pointer), count * sizeof(T)), operation);
    return std::unique_ptr<T, HipBufferDeleter>(pointer);
}

struct HiprtContextOwner {
    hiprtContext value = nullptr;
    bool owns = true;
    explicit HiprtContextOwner(hiprtContext borrowed = nullptr) : value(borrowed), owns(!borrowed) {}
    HiprtContextOwner(const HiprtContextOwner&) = delete;
    HiprtContextOwner& operator=(const HiprtContextOwner&) = delete;
    ~HiprtContextOwner() {
        if (value && owns) {
            const hiprtError result = hiprtDestroyContext(value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT context: error " << result << '\n';
            }
        }
    }
};

struct HiprtSceneOwner {
    hiprtContext context = nullptr;
    hiprtScene value = nullptr;
    HiprtSceneOwner() = default;
    HiprtSceneOwner(const HiprtSceneOwner&) = delete;
    HiprtSceneOwner& operator=(const HiprtSceneOwner&) = delete;
    ~HiprtSceneOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyScene(context, value);
            if (result != hiprtSuccess) std::cerr << "Destroying HIP RT scene: error " << result << '\n';
        }
    }
};

struct HiprtGeometryOwner {
    hiprtContext context = nullptr;
    hiprtGeometry value = nullptr;
    HiprtGeometryOwner() = default;
    HiprtGeometryOwner(const HiprtGeometryOwner&) = delete;
    HiprtGeometryOwner& operator=(const HiprtGeometryOwner&) = delete;
    ~HiprtGeometryOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyGeometry(context, value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT geometry: error " << result << '\n';
            }
        }
    }
};

struct HiprtFunctionTableOwner {
    hiprtContext context = nullptr;
    hiprtFuncTable value = nullptr;
    HiprtFunctionTableOwner() = default;
    HiprtFunctionTableOwner(const HiprtFunctionTableOwner&) = delete;
    HiprtFunctionTableOwner& operator=(const HiprtFunctionTableOwner&) = delete;
    ~HiprtFunctionTableOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyFuncTable(context, value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT function table: error " << result << '\n';
            }
        }
    }
};

}
