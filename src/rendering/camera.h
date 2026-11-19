#pragma once

#include "rendering/integrator.h"

namespace hip_demo {

template<bool Instanced = true, bool Unified = false>
__device__ inline float3 sample_pixel(int x, int y, int width, int height, int sample, float3 forward, float3 right,
                               float3 up, TraceScene scene, bool* specular_primary = nullptr) {
    const float half_view_height = scene.settings->half_view_height;
    const float aspect_ratio = static_cast<float>(width) / height;
    const std::uint32_t seed = static_cast<std::uint32_t>(
        (x + y * width + 1) * 747796405u + (sample + 1) * 2891336453u);
    Random random{seed == 0 ? 1u : seed};
    const float u = (2.0f * (static_cast<float>(x) + random.next()) / width - 1.0f) *
        aspect_ratio * half_view_height;
    const float v = (1.0f - 2.0f * (static_cast<float>(y) + random.next()) / height) *
        half_view_height;
    const float3 direction = normalize(forward + u * right + v * up);
    return trace<Instanced, Unified>({vector3(scene.settings->camera_position), direction}, random, scene, specular_primary);
}

}
