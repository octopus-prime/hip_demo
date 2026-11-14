#pragma once

#include "math/vector.h"
#include <cstdint>

namespace hip_demo {

__device__ inline float tone_map(float value) {
    value = fmaxf(0.0f, value);
    return sqrtf(value / (1.0f + value));
}

__device__ inline std::uint32_t encode_color(float3 color) {
    const auto red = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.x)));
    const auto green = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.y)));
    const auto blue = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.z)));
    return red | (green << 8) | (blue << 16);
}

}
