#pragma once

#include "math/vector.h"
#include <hiprt/hiprt_math.h>

struct DielectricSplit {
    float3 transmitted{0, 0, 0};
    float fresnel = 1;
};

__device__ inline DielectricSplit split_dielectric(float3 direction, float3 outward_normal, float ior)
{
    if (ior == 1.0f) {
        return {direction, 0};
    }
    const bool entering = hiprt::dot(direction, outward_normal) < 0;
    const auto normal = entering ? outward_normal : -outward_normal;
    const float eta = entering ? 1.0f / ior : ior;
    DielectricSplit split;
    if (!hip_demo::refract(direction, normal, eta, split.transmitted)) {
        return split;
    }
    split.transmitted = hiprt::normalize(split.transmitted);
    const float incident_cosine = fminf(1.0f, fmaxf(0.0f, hiprt::dot(-direction, normal)));
    const float transmitted_cosine = fminf(1.0f, fmaxf(0.0f, hiprt::dot(-split.transmitted, normal)));
    const float s = (eta * incident_cosine - transmitted_cosine) /
                    (eta * incident_cosine + transmitted_cosine);
    const float p = (incident_cosine - eta * transmitted_cosine) /
                    (incident_cosine + eta * transmitted_cosine);
    split.fresnel = fminf(1.0f, fmaxf(0.0f, 0.5f * (s * s + p * p)));
    return split;
}
