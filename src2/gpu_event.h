#pragma once

#include "error.h"

#include <iostream>

class GpuEvent {
public:
    GpuEvent() { check_hip(hipEventCreate(&event_), "Creating GPU timing event"); }
    ~GpuEvent() noexcept
    {
        const auto result = hipEventDestroy(event_);
        if (result != hipSuccess) {
            std::cerr << "Destroying GPU timing event: " << hipGetErrorString(result) << '\n';
        }
    }

    GpuEvent(const GpuEvent&) = delete;
    GpuEvent& operator=(const GpuEvent&) = delete;

    hipEvent_t get() const noexcept { return event_; }

private:
    hipEvent_t event_ = nullptr;
};
