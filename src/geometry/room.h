#pragma once

#include "geometry/hit.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline bool intersect_room(const Ray& ray, float maximum_distance, Hit& hit, int& wall_index,
                         const GpuSceneSettings* settings = nullptr) {
    const float half_width = settings ? settings->room_size.x / 2 : room_half_width;
    const float height = settings ? settings->room_size.y : room_height;
    const float half_depth = settings ? settings->room_size.z / 2 : room_half_depth;
    bool found = false;
    for (int wall = 0; wall < 5; ++wall) {
        float origin, direction, position;
        float3 normal;
        if (wall < 2) {
            origin = ray.origin.y;
            direction = ray.direction.y;
            position = wall == 0 ? 0.0f : height;
            normal = wall == 0 ? float3{0.0f, 1.0f, 0.0f} : float3{0.0f, -1.0f, 0.0f};
        } else if (wall == 2) {
            origin = ray.origin.z;
            direction = ray.direction.z;
            position = -half_depth;
            normal = {0.0f, 0.0f, 1.0f};
        } else {
            origin = ray.origin.x;
            direction = ray.direction.x;
            position = wall == 3 ? -half_width : half_width;
            normal = wall == 3 ? float3{1.0f, 0.0f, 0.0f} : float3{-1.0f, 0.0f, 0.0f};
        }
        if (fabsf(direction) < 1.0e-6f) {
            continue;
        }
        const float distance = (position - origin) / direction;
        if (distance < 0.001f || distance >= maximum_distance) {
            continue;
        }
        const float3 point = ray.origin + distance * ray.direction;
        if (fabsf(point.x) > half_width + 0.0001f ||
            point.y < -0.0001f || point.y > height + 0.0001f ||
            fabsf(point.z) > half_depth + 0.0001f) {
            continue;
        }
        maximum_distance = distance;
        hit.distance = distance;
        hit.point = point;
        set_face_normal(hit, ray, normal);
        wall_index = wall;
        found = true;
    }
    return found;
}

}
