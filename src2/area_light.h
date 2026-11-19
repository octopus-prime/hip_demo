#pragma once

#include "scene.h"
#include "light/types.h"
#include <hiprt/hiprt_device.h>
#include <hiprt/hiprt_math.h>

struct WorldRectangle {
    float3 center;
    float3 half_u;
    float3 half_v;
    float3 normal;
    float area;
};

__device__ inline WorldRectangle world_rectangle(hiprtScene scene, unsigned int instance_id)
{
    const auto frame = hiprtGetObjectToWorldFrameMatrix(scene, instance_id);
    const auto& m = frame.matrix;
    const float3 u{m[0][0], m[1][0], m[2][0]};
    const float3 v{m[0][2], m[1][2], m[2][2]};
    const auto cross = hiprt::cross(v, u);
    const float length = sqrtf(hiprt::dot(cross, cross));
    return {{m[0][3], m[1][3], m[2][3]}, u, v, cross / length, 4.0f * length};
}

__device__ inline double rectangle_light_pdf(const WorldRectangle& light, unsigned int light_count,
                                             float3 from, float3 point)
{
    const auto offset = point - from;
    const double distance_squared = double(offset.x) * offset.x + double(offset.y) * offset.y +
                                    double(offset.z) * offset.z;
    if (distance_squared == 0) {
        return 0;
    }
    const float cosine = hiprt::dot(light.normal, -hiprt::normalize(offset));
    return hip_demo::solid_angle_pdf((1.0f / light.area) / light_count, distance_squared, cosine);
}
