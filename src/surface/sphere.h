#pragma once

#include "surface/hit.h"
#include "math/polynomial.h"

namespace hip_demo {

__device__ inline bool hit_sphere(const Ray& ray, float3 center, float radius, float maximum_distance, Hit& hit) {
    const float3 offset = ray.origin - center;
    const float a = dot(ray.direction, ray.direction);
    const float b = 2.0f * dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const auto roots = quadratic_roots(a, b, c);
    if (roots.count == 0) return false;
    float distance = roots.values[0];
    if (distance < 0.001f || distance >= maximum_distance) {
        if (roots.count < 2) return false;
        distance = roots.values[1];
    }
    if (distance < 0.001f || distance >= maximum_distance) return false;
    hit.distance = distance;
    hit.point = ray.origin + distance * ray.direction;
    set_face_normal(hit, ray, (hit.point - center) / radius);
    return true;
}

}
