#pragma once

#include "surface/hit.h"

namespace hip_demo {

__device__ inline bool hit_sphere(const Ray& ray, float3 center, float radius, float maximum_distance, Hit& hit) {
    const float3 offset = ray.origin - center;
    const float half_b = dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = half_b * half_b - c;
    if (discriminant < 0.0f) {
        return false;
    }

    const float root = sqrtf(discriminant);
    float distance = -half_b - root;
    if (distance < 0.001f || distance >= maximum_distance) {
        distance = -half_b + root;
    }
    if (distance < 0.001f || distance >= maximum_distance) {
        return false;
    }

    hit.distance = distance;
    hit.point = ray.origin + distance * ray.direction;
    set_face_normal(hit, ray, (hit.point - center) / radius);
    return true;
}

}
