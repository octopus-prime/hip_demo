#pragma once

#include "error.h"

#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <type_traits>

template <typename T>
class DeviceBuffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    explicit DeviceBuffer(std::size_t count) : count_(count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::overflow_error("GPU buffer size is too large.");
        }
        if (count != 0) {
            check_hip(hipMalloc(reinterpret_cast<void**>(&data_), count * sizeof(T)),
                      "Allocating GPU buffer");
        }
    }

    ~DeviceBuffer() noexcept
    {
        if (data_) {
            const hipError_t result = hipFree(data_);
            if (result != hipSuccess) {
                std::cerr << "Freeing GPU buffer: " << hipGetErrorString(result) << '\n';
            }
        }
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    void upload(std::span<const T> values)
    {
        if (values.size() != count_) {
            throw std::invalid_argument("Upload size must match the GPU buffer element count.");
        }
        if (!values.empty()) {
            check_hip(hipMemcpy(data_, values.data(), values.size_bytes(), hipMemcpyHostToDevice),
                      "Uploading GPU buffer");
        }
    }

    void download(std::span<T> values) const
    {
        if (values.size() != count_) {
            throw std::invalid_argument("Download size must match the GPU buffer element count.");
        }
        if (!values.empty()) {
            check_hip(hipMemcpy(values.data(), data_, values.size_bytes(), hipMemcpyDeviceToHost),
                      "Downloading GPU buffer");
        }
    }

    T* get() const noexcept { return data_; }

private:
    T* data_ = nullptr;
    std::size_t count_;
};
