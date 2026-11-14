#pragma once

#include "surface/hit.h"
#include "scene/defaults.h"
#include "math/polynomial.h"

namespace hip_demo {

__device__ inline float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ inline bool hit_torus(const Ray& ray, float3 center, float maximum_distance, Hit& hit,
                          float major_radius = torus_major_radius, float minor_radius = torus_minor_radius) {
    const float bound = major_radius + minor_radius;
    const float3 offset = ray.origin - center;
    const float speed_squared = dot(ray.direction, ray.direction);
    if (speed_squared == 0.0f) return false;
    const float speed = sqrtf(speed_squared);
    const float middle = -dot(offset, ray.direction) / speed_squared;
    const float bound_time = bound / speed;
    if (middle + bound_time < 0.001f || middle - bound_time >= maximum_distance) return false;

    // Center the ray and scale by a power of two, preserving FP32 coefficients
    // while keeping quartic roots near [-1,1].
    int exponent = 0;
    frexpf(bound, &exponent);
    const float scale = scalbnf(1.0f, -exponent), scaled_bound = bound * scale;
    const float time_scale = 1.0f / (scale * speed);
    const float3 o = float3{fmaf(middle, ray.direction.x, offset.x),
        fmaf(middle, ray.direction.y, offset.y), fmaf(middle, ray.direction.z, offset.z)} * scale;
    if (dot(o, o) > scaled_bound * scaled_bound * (1.0f + 8.0f * 0x1p-23f)) return false;
    const float3 d = ray.direction / speed;
    const float R = major_radius * scale, r = minor_radius * scale;
    const float dd = dot(d, d), od = dot(o, d);
    const float k = dot(o, o) + (R - r) * (R + r), four_R2 = 4.0f * R * R;
    const auto roots = quartic_roots(dd * dd, 4.0f * dd * od,
        fmaf(2.0f * dd, k, fmaf(4.0f * od, od, -four_R2 * (d.x * d.x + d.z * d.z))),
        fmaf(4.0f * od, k, -2.0f * four_R2 * (o.x * d.x + o.z * d.z)),
        fmaf(k, k, -four_R2 * (o.x * o.x + o.z * o.z)));
    for (int i = 0; i < roots.count; ++i) {
        const float distance = fmaf(roots.values[i], time_scale, middle);
        if (distance < 0.001f || distance >= maximum_distance) continue;
        const float3 p = o + roots.values[i] * d;
        const float sum = dot(p, p) + (R - r) * (R + r);
        const float3 normal = normalize({p.x * (sum - 2.0f * R * R),
            p.y * sum, p.z * (sum - 2.0f * R * R)});
        hit.distance = distance;
        hit.point = ray.origin + distance * ray.direction;
        set_face_normal(hit, ray, normal);
        return true;
    }
    return false;
}

}
