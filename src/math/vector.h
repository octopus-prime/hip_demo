#pragma once

#include <hip/hip_runtime.h>
#include <cmath>
#include "scene/types.h"

namespace hip_demo {

__host__ __device__ inline float3 rotate_x(float3 value, float sine, float cosine) {
    return {value.x, cosine * value.y - sine * value.z, sine * value.y + cosine * value.z};
}

__host__ __device__ inline constexpr float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ inline float3 cross(float3 a, float3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

__device__ inline float3 normalize(float3 value) {
    return value / sqrtf(dot(value, value));
}

__device__ inline float clamp01(float value) {
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ inline float3 reflect(float3 direction, float3 normal) {
    return direction - normal * (2.0f * dot(direction, normal));
}

__device__ inline bool refract(float3 direction, float3 normal, float eta, float3& result) {
    const float cos_theta = fminf(dot(-direction, normal), 1.0f);
    const float3 perpendicular = eta * (direction + cos_theta * normal);
    const float parallel_squared = 1.0f - dot(perpendicular, perpendicular);
    if (parallel_squared < 0.0f) {
        return false;
    }
    result = perpendicular - sqrtf(parallel_squared) * normal;
    return true;
}

__host__ __device__ inline float3 vector3(SceneVector value) {
    return {value.x, value.y, value.z};
}

}
