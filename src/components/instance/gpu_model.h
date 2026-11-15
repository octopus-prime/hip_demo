#pragma once

#include "components/instance/model.h"
#include "components/material/gpu_model.h"
#include "math/vector.h"
#include <limits>
#include <numbers>
#include <stdexcept>

namespace hip_demo {
struct GpuInstanceTransform {
    SceneVector rows[3], inverse[3], position;
    double determinant;
};
struct GpuInstance {
    GpuInstanceTransform transform;
    GpuMaterial material;
    unsigned int prototype, object_id;
    bool material_override;
};
}

namespace hip_demo::instance {
inline GpuInstanceTransform prepare_transform(const InstanceTransform& source) {
    const auto angle = [](float degrees) { return std::remainder(double(degrees), 360.0) * std::numbers::pi / 180; };
    const double x = angle(source.rotation.x), y = angle(source.rotation.y), z = angle(source.rotation.z);
    const double cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y),
                 cz = std::cos(z), sz = std::sin(z);
    const double rotation[3][3]{
        {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
        {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
        {-sy, cy * sx, cy * cx}
    };
    const double scale[3]{source.scale.x, source.scale.y, source.scale.z};
    const auto checked = [](double value) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            throw std::runtime_error("Instance transformation exceeds the finite float range");
        return static_cast<float>(value);
    };
    for (double value : scale)
        if (!std::isfinite(value) || value <= 0) throw std::runtime_error("Instance scale must be finite and positive");
    GpuInstanceTransform result{};
    result.position = {checked(source.position.x), checked(source.position.y), checked(source.position.z)};
    for (int i = 0; i < 3; ++i) {
        result.rows[i] = {checked(rotation[i][0] * scale[0]), checked(rotation[i][1] * scale[1]),
                          checked(rotation[i][2] * scale[2])};
        result.inverse[i] = {checked(rotation[0][i] / scale[i]), checked(rotation[1][i] / scale[i]),
                             checked(rotation[2][i] / scale[i])};
    }
    result.determinant = scale[0] * scale[1] * scale[2];
    return result;
}

__device__ inline float3 point_to_world(const GpuInstanceTransform& t, float3 p) {
    return {dot(vector3(t.rows[0]), p) + t.position.x, dot(vector3(t.rows[1]), p) + t.position.y,
            dot(vector3(t.rows[2]), p) + t.position.z};
}
__device__ inline float3 vector_to_world(const GpuInstanceTransform& t, float3 v) {
    return {dot(vector3(t.rows[0]), v), dot(vector3(t.rows[1]), v), dot(vector3(t.rows[2]), v)};
}
__device__ inline float3 point_to_local(const GpuInstanceTransform& t, float3 p) {
    p = p - vector3(t.position);
    return {dot(vector3(t.inverse[0]), p), dot(vector3(t.inverse[1]), p), dot(vector3(t.inverse[2]), p)};
}
__device__ inline float3 vector_to_local(const GpuInstanceTransform& t, float3 v) {
    return {dot(vector3(t.inverse[0]), v), dot(vector3(t.inverse[1]), v), dot(vector3(t.inverse[2]), v)};
}
__device__ inline float3 normalized_normal(float3 value) {
    const float maximum = fmaxf(fabsf(value.x), fmaxf(fabsf(value.y), fabsf(value.z)));
    return normalize(value / maximum);
}
__device__ inline float3 normal_vector(const GpuInstanceTransform& t, float3 n) {
    return {t.inverse[0].x * n.x + t.inverse[1].x * n.y + t.inverse[2].x * n.z,
            t.inverse[0].y * n.x + t.inverse[1].y * n.y + t.inverse[2].y * n.z,
            t.inverse[0].z * n.x + t.inverse[1].z * n.y + t.inverse[2].z * n.z};
}
__device__ inline float3 normal_to_world(const GpuInstanceTransform& t, float3 n) {
    const auto value = normal_vector(t, n);
    return normalized_normal(value);
}
__device__ inline float3 normal_to_local(const GpuInstanceTransform& t, float3 n) {
    const float3 value{t.rows[0].x * n.x + t.rows[1].x * n.y + t.rows[2].x * n.z,
                       t.rows[0].y * n.x + t.rows[1].y * n.y + t.rows[2].y * n.z,
                       t.rows[0].z * n.x + t.rows[1].z * n.y + t.rows[2].z * n.z};
    return normalized_normal(value);
}
__device__ inline double area_scale(const GpuInstanceTransform& t, float3 local_normal) {
    // Surface area transforms by |det(M)| * length(M^-T * unit_normal).
    const auto value = normal_vector(t, local_normal);
    return t.determinant * sqrt(double(value.x) * value.x + double(value.y) * value.y + double(value.z) * value.z);
}
}
