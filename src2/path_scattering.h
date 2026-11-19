#pragma once

#include "dielectric.h"
#include "scene.h"
#include "math/random.h"
#include "scene/defaults.h"

struct PathScatter {
    float3 direction;
    float3 weight;
    float pdf = 0;
};

__device__ inline float3 sample_cosine_hemisphere(float3 normal, hip_demo::Random& random)
{
    const float u = random.next();
    const float angle = 2.0f * hip_demo::pi * random.next();
    const auto reference = fabsf(normal.y) < 0.99f ? float3{0, 1, 0} : float3{1, 0, 0};
    const auto tangent = hiprt::normalize(hiprt::cross(reference, normal));
    const auto bitangent = hiprt::cross(normal, tangent);
    return hiprt::normalize(sqrtf(u) * (cosf(angle) * tangent + sinf(angle) * bitangent) +
                            sqrtf(1.0f - u) * normal);
}

template <bool Glass>
__device__ inline PathScatter sample_path_scatter(const SurfaceMaterial& material, float3 incoming,
                                                 float3 outward_normal, hip_demo::Random& random)
{
    const auto reflected = hiprt::normalize(hip_demo::reflect(incoming, outward_normal));
    if (material.reflectivity > 0.0f && random.next() < material.reflectivity) {
        return {reflected, {1, 1, 1}};
    }
    if constexpr (Glass) {
        if (material.transmission > 0.0f && random.next() < material.transmission) {
            const auto split = split_dielectric(incoming, outward_normal, material.ior);
            if (random.next() < split.fresnel) {
                return {reflected, {1, 1, 1}};
            }
            const float eta = hiprt::dot(incoming, outward_normal) < 0.0f
                ? 1.0f / material.ior : material.ior;
            // Radiance transport scales by eta^2; entry/exit factors cancel through a solid.
            const float weight = eta * eta;
            return {split.transmitted, {weight, weight, weight}};
        }
    }
    const auto normal = hiprt::dot(incoming, outward_normal) < 0.0f ? outward_normal : -outward_normal;
    // Lambert BSDF * cosine / cosine-weighted PDF reduces to the albedo.
    const auto direction = sample_cosine_hemisphere(normal, random);
    const float pdf = (1.0f - material.reflectivity) * (1.0f - material.transmission) *
                      fmaxf(0.0f, hiprt::dot(normal, direction)) / hip_demo::pi;
    return {direction, material.base_color, pdf};
}
