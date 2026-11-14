#pragma once

#include "texture/noise.h"

namespace hip_demo {

__device__ inline float3 marble_albedo(float3 point) {
    const float x = point.x * 0.7f;
    const float y = point.y * 0.7f;
    const float warp_x = fractal_noise(x + 3.1f, y - 7.4f);
    const float warp_y = fractal_noise(x - 5.8f, y + 1.6f);
    const float flow = fractal_noise(x + 2.0f * warp_x, y + 2.0f * warp_y);
    const float vein = powf(0.5f + 0.5f * sinf(2.8f * point.x + 1.6f * point.y + 6.0f * flow), 12.0f);
    const float3 stone = (0.90f + 0.07f * warp_x) * float3{1.0f, 0.98f, 0.96f};
    return (1.0f - vein) * stone + vein * float3{0.12f, 0.16f, 0.20f};
}

}
