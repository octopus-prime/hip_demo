#pragma once

#include "geometry/hit.h"
#include "scene/defaults.h"
#include "math/torus_roots.h"

namespace hip_demo {

__device__ inline float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ inline Ray Torus::local_ray(const Ray& ray) const {
    return {rotate_x(ray.origin - vector3(center), -tilt_sine, tilt_cosine),
            rotate_x(ray.direction, -tilt_sine, tilt_cosine)};
}

__device__ inline TorusRoots Torus::intersections(const Ray& world_ray, float minimum, float maximum) const {
    const auto ray = local_ray(world_ray);
    return torus_roots(ray.origin, ray.direction, major_radius, minor_radius, minimum, maximum);
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
