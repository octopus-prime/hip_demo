#pragma once

#include "texture/noise.h"

namespace hip_demo {

__device__ inline float3 wood_albedo(float3 point) {
    const float warp = value_noise(point.x * 1.2f, point.y * 0.65f);
    const float detail = value_noise(point.x * 5.0f, point.y * 0.8f);
    const float fine = value_noise(point.x * 22.0f, point.y * 1.2f);
    const float grain = 0.5f + 0.5f * sinf(point.x * 35.0f + 12.0f * warp + 3.0f * detail);
    const float tone = clamp01(0.10f + 0.48f * grain + 0.27f * detail + 0.15f * fine);
    return (1.0f - tone) * float3{0.25f, 0.10f, 0.035f} + tone * float3{0.66f, 0.36f, 0.14f};
}

}
