#pragma once

#include "components/geometry/box/model.h"
#include "geometry/hit.h"

namespace hip_demo {
struct BoxIntersections {
    float entry = 0, exit = 0;
    float3 entry_normal{}, exit_normal{};
    bool found = false;
};

__device__ inline BoxIntersections Box::intersections(const Ray& ray, bool solid) const {
    const float3 offset = ray.origin - vector3(center);
    const float origins[]{offset.x, offset.y, offset.z};
    const float directions[]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float extents[]{size.x / 2, size.y / 2, size.z / 2};
    BoxIntersections result;
    result.entry = -INFINITY;
    result.exit = INFINITY;
    for (int axis = 0; axis < 3; ++axis) {
        if (directions[axis] == 0) {
            if (solid ? fabsf(origins[axis]) >= extents[axis] : fabsf(origins[axis]) > extents[axis])
                return {};
            continue;
        }
        const float a = (-extents[axis] - origins[axis]) / directions[axis];
        const float b = (extents[axis] - origins[axis]) / directions[axis];
        const float sign = directions[axis] > 0 ? -1.0f : 1.0f;
        const auto normal = axis_vector(static_cast<Axis>(axis), sign);
        if (fminf(a, b) > result.entry) {
            result.entry = fminf(a, b);
            result.entry_normal = normal;
        }
        if (fmaxf(a, b) < result.exit) {
            result.exit = fmaxf(a, b);
            result.exit_normal = -normal;
        }
        if (result.entry > result.exit) return {};
    }
    result.found = isfinite(result.entry) && isfinite(result.exit);
    return result;
}

__device__ inline bool Box::intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const {
    const auto roots = intersections(ray);
    if (!roots.found) return false;
    const bool entering = roots.entry >= minimum;
    const float distance = entering ? roots.entry : roots.exit;
    if (distance < minimum || distance >= maximum) return false;
    hit.distance = distance;
    hit.point = ray.origin + distance * ray.direction;
    set_face_normal(hit, ray, entering ? roots.entry_normal : roots.exit_normal);
    return true;
}

__device__ inline bool Box::same_shape(const Box& other) const {
    return center.x == other.center.x && center.y == other.center.y && center.z == other.center.z &&
        size.x == other.size.x && size.y == other.size.y && size.z == other.size.z;
}
}
