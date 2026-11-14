#pragma once

#include "surface/sphere.h"
#include "surface/torus.h"

namespace hip_demo {

__device__ inline bool hit_described_primitive(const Ray& ray, const ScenePrimitive& object,
                                       float maximum_distance, Hit& hit) {
    const float3 center = vector3(object.center);
    if (object.sphere) {
        return hit_sphere(ray, center, object.radius, maximum_distance, hit);
    }
    const Ray local_ray{
        rotate_x(ray.origin - center, -object.tilt_sine, object.tilt_cosine),
        rotate_x(ray.direction, -object.tilt_sine, object.tilt_cosine)};
    if (!hit_torus(local_ray, {0, 0, 0}, maximum_distance, hit, object.major_radius, object.minor_radius)) {
        return false;
    }
    hit.point = ray.origin + hit.distance * ray.direction;
    hit.normal = rotate_x(hit.normal, object.tilt_sine, object.tilt_cosine);
    return true;
}

}
