#pragma once

#include "surface/hit.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ inline bool hit_torus(const Ray& ray, float3 center, float maximum_distance, Hit& hit,
                          float major_radius = torus_major_radius, float minor_radius = torus_minor_radius) {
    const float bound_radius = major_radius + minor_radius;

    const float3 bound_offset = ray.origin - center;
    const float half_b = dot(bound_offset, ray.direction);
    const float discriminant = half_b * half_b -
        (dot(bound_offset, bound_offset) - bound_radius * bound_radius);
    if (discriminant < 0.0f) {
        return false;
    }

    const float root = sqrtf(discriminant);
    const float start = fmaxf(0.001f, -half_b - root);
    const float end = fminf(maximum_distance, -half_b + root);
    if (start >= end) {
        return false;
    }

    float distance = start;
    bool found = false;
    for (int step = 0; step < 128 && distance < end; ++step) {
        const float3 point = ray.origin + distance * ray.direction;
        const float signed_distance = torus_distance(point, center, major_radius, minor_radius);
        if (fabsf(signed_distance) < 0.001f) {
            found = true;
            break;
        }
        distance += fmaxf(fabsf(signed_distance) * 0.75f, 0.0005f);
    }
    if (!found || distance >= maximum_distance) {
        return false;
    }

    const float3 point = ray.origin + distance * ray.direction;
    constexpr float epsilon = 0.001f;
    const float3 normal = normalize({
        torus_distance(point + float3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - float3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + float3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - float3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + float3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius) -
            torus_distance(point - float3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius),
    });

    hit.distance = distance;
    hit.point = point;
    set_face_normal(hit, ray, normal);
    return true;
}

}
