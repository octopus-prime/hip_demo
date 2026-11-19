#pragma once

#include "polynomial.h"
#include <hip/hip_runtime.h>
#include <hiprt/hiprt_math.h>

namespace hip_demo {

struct TorusRoots {
    PolynomialRoots roots;
    float3 origin{}, direction{};
    float middle = 0, time_scale = 1, major = 0, minor = 0;

    __device__ float distance(int index) const {
        return fmaf(roots.values[index], time_scale, middle);
    }

    __device__ float3 normal(int index) const {
        const float3 p = origin + roots.values[index] * direction;
        const float sum = hiprt::dot(p, p) + (major - minor) * (major + minor);
        return hiprt::normalize(float3{p.x * (sum - 2.0f * major * major), p.y * sum,
                                      p.z * (sum - 2.0f * major * major)});
    }
};

__device__ inline TorusRoots torus_roots(float3 offset, float3 direction, float major_radius,
                                        float minor_radius, float minimum, float maximum) {
    const float bound = major_radius + minor_radius;
    const float speed_squared = hiprt::dot(direction, direction);
    if (speed_squared == 0.0f) return {};
    const float speed = sqrtf(speed_squared);
    const float middle = -hiprt::dot(offset, direction) / speed_squared;
    const float bound_time = bound / speed;
    if (middle + bound_time < minimum || middle - bound_time >= maximum) return {};

    // Center the ray and scale by a power of two, preserving FP32 coefficients
    // while keeping quartic roots near [-1,1].
    int exponent = 0;
    frexpf(bound, &exponent);
    const float scale = scalbnf(1.0f, -exponent), scaled_bound = bound * scale;
    const float time_scale = 1.0f / (scale * speed);
    const float3 o = float3{fmaf(middle, direction.x, offset.x),
        fmaf(middle, direction.y, offset.y), fmaf(middle, direction.z, offset.z)} * scale;
    if (hiprt::dot(o, o) > scaled_bound * scaled_bound * (1.0f + 8.0f * 0x1p-23f)) return {};
    const float3 d = direction / speed;
    const float R = major_radius * scale, r = minor_radius * scale;
    const float dd = hiprt::dot(d, d), od = hiprt::dot(o, d);
    const float k = hiprt::dot(o, o) + (R - r) * (R + r), four_R2 = 4.0f * R * R;
    const auto roots = quartic_roots(dd * dd, 4.0f * dd * od,
        fmaf(2.0f * dd, k, fmaf(4.0f * od, od, -four_R2 * (d.x * d.x + d.z * d.z))),
        fmaf(4.0f * od, k, -2.0f * four_R2 * (o.x * d.x + o.z * d.z)),
        fmaf(k, k, -four_R2 * (o.x * o.x + o.z * o.z)));
    return {roots, o, d, middle, time_scale, R, r};
}

}
