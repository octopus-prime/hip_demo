#pragma once

#include "geometry/hit.h"
#include "math/polynomial.h"

namespace hip_demo {

__device__ inline PolynomialRoots Sphere::intersections(const Ray& ray) const {
    const float3 offset = ray.origin - vector3(center);
    const float a = dot(ray.direction, ray.direction);
    const float b = 2.0f * dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    return quadratic_roots(a, b, c);
}

__device__ inline float3 Sphere::normal_at(const Ray& ray, float distance) const {
    const float3 offset = ray.origin - vector3(center);
    return normalize({fmaf(distance, ray.direction.x, offset.x),
                      fmaf(distance, ray.direction.y, offset.y),
                      fmaf(distance, ray.direction.z, offset.z)});
}

__device__ inline bool Sphere::intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const {
    const auto roots = intersections(ray);
    for (int i = 0; i < roots.count; ++i) {
        const float distance = roots.values[i];
        if (distance < minimum || distance >= maximum) continue;
        hit.distance = distance;
        hit.point = ray.origin + distance * ray.direction;
        set_face_normal(hit, ray, (hit.point - vector3(center)) / radius);
        return true;
    }
    return false;
}

}
