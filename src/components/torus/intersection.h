#pragma once

#include "geometry/hit.h"
#include "scene/defaults.h"
#include "math/polynomial.h"

namespace hip_demo {

__device__ inline float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

struct TorusRoots {
    PolynomialRoots roots;
    float3 origin{}, direction{};
    float middle = 0, time_scale = 1, major = 0, minor = 0;

    __device__ float distance(int index) const {
        return fmaf(roots.values[index], time_scale, middle);
    }

    __device__ float3 normal(int index) const {
        const float3 p = origin + roots.values[index] * direction;
        const float sum = dot(p, p) + (major - minor) * (major + minor);
        return normalize({p.x * (sum - 2.0f * major * major), p.y * sum,
                          p.z * (sum - 2.0f * major * major)});
    }
};

__device__ inline Ray Torus::local_ray(const Ray& ray) const {
    return {rotate_x(ray.origin - vector3(center), -tilt_sine, tilt_cosine),
            rotate_x(ray.direction, -tilt_sine, tilt_cosine)};
}

__device__ inline TorusRoots Torus::intersections(const Ray& world_ray, float minimum, float maximum) const {
    const auto ray = local_ray(world_ray);
    const float bound = major_radius + minor_radius;
    const float3 offset = ray.origin;
    const float speed_squared = dot(ray.direction, ray.direction);
    if (speed_squared == 0.0f) return {};
    const float speed = sqrtf(speed_squared);
    const float middle = -dot(offset, ray.direction) / speed_squared;
    const float bound_time = bound / speed;
    if (middle + bound_time < minimum || middle - bound_time >= maximum) return {};

    // Center the ray and scale by a power of two, preserving FP32 coefficients
    // while keeping quartic roots near [-1,1].
    int exponent = 0;
    frexpf(bound, &exponent);
    const float scale = scalbnf(1.0f, -exponent), scaled_bound = bound * scale;
    const float time_scale = 1.0f / (scale * speed);
    const float3 o = float3{fmaf(middle, ray.direction.x, offset.x),
        fmaf(middle, ray.direction.y, offset.y), fmaf(middle, ray.direction.z, offset.z)} * scale;
    if (dot(o, o) > scaled_bound * scaled_bound * (1.0f + 8.0f * 0x1p-23f)) return {};
    const float3 d = ray.direction / speed;
    const float R = major_radius * scale, r = minor_radius * scale;
    const float dd = dot(d, d), od = dot(o, d);
    const float k = dot(o, o) + (R - r) * (R + r), four_R2 = 4.0f * R * R;
    const auto roots = quartic_roots(dd * dd, 4.0f * dd * od,
        fmaf(2.0f * dd, k, fmaf(4.0f * od, od, -four_R2 * (d.x * d.x + d.z * d.z))),
        fmaf(4.0f * od, k, -2.0f * four_R2 * (o.x * d.x + o.z * d.z)),
        fmaf(k, k, -four_R2 * (o.x * o.x + o.z * o.z)));
    return {roots, o, d, middle, time_scale, R, r};
}

__device__ inline bool Torus::intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const {
    const auto roots = intersections(ray, minimum, maximum);
    for (int i = 0; i < roots.roots.count; ++i) {
        const float distance = roots.distance(i);
        if (distance < minimum || distance >= maximum) continue;
        hit.distance = distance;
        hit.point = ray.origin + distance * ray.direction;
        set_face_normal(hit, ray, rotate_x(roots.normal(i), tilt_sine, tilt_cosine));
        return true;
    }
    return false;
}

}
