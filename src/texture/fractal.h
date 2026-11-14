#pragma once

#include "math/vector.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline float3 fractal_wall_albedo(float3 point, bool julia, float depth = 2 * room_half_depth,
                                     float height = room_height) {
    const float horizontal = (julia ? point.z : -point.z) / depth + 0.5f;
    const float real = 3.5f * horizontal - (julia ? 1.75f : 2.25f);
    const float imaginary = 0.7f * (point.y - height * 0.5f);
    const float c_real = julia ? -0.8f : real;
    const float c_imaginary = julia ? 0.156f : imaginary;
    float x = julia ? real : 0.0f;
    float y = julia ? imaginary : 0.0f;
    float magnitude_squared = x * x + y * y;
    constexpr int max_iterations = 48;
    int iteration = 0;
    while (iteration < max_iterations && magnitude_squared <= 4.0f) {
        const float next_x = x * x - y * y + c_real;
        y = 2.0f * x * y + c_imaginary;
        x = next_x;
        magnitude_squared = x * x + y * y;
        ++iteration;
    }
    if (magnitude_squared <= 4.0f) {
        return julia ? float3{0.015f, 0.06f, 0.025f} : float3{0.07f, 0.012f, 0.015f};
    }
    const float smooth_iteration = static_cast<float>(iteration) + 1.0f -
        log2f(0.5f * log2f(magnitude_squared));
    const float band = 0.5f + 0.5f * cosf(0.32f * smooth_iteration);
    const float3 dark = julia ? float3{0.04f, 0.25f, 0.08f} : float3{0.30f, 0.035f, 0.025f};
    const float3 light = julia ? float3{0.40f, 0.78f, 0.28f} : float3{0.88f, 0.30f, 0.12f};
    return (1.0f - band) * dark + band * light;
}

}
