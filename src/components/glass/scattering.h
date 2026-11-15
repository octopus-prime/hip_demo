#pragma once

#include "components/glass/model.h"
#include "geometry/hit.h"
#include "math/random.h"

namespace hip_demo::glass {

__device__ inline float reflectance(float cosine, float refraction_index) {
    float base_reflectance = (1.0f - refraction_index) / (1.0f + refraction_index);
    base_reflectance *= base_reflectance;
    return base_reflectance + (1.0f - base_reflectance) *
        powf(1.0f - cosine, 5.0f);
}

__device__ inline float3 Shader::evaluate(const Ray&, const Hit&, float3, float) const {
    return {};
}

__device__ inline bool Shader::scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const {
    constexpr float refraction_index = 1.5f;
    const float eta = hit.front_face ? 1.0f / refraction_index : refraction_index;
    const float cosine = fminf(dot(-ray.direction, hit.normal), 1.0f);
    const float sine = sqrtf(1.0f - cosine * cosine);
    const bool total_internal_reflection = eta * sine > 1.0f;
    const bool should_reflect = total_internal_reflection ||
        reflectance(cosine, refraction_index) > random.next();
    float3 refracted{};
    if (should_reflect || !refract(ray.direction, hit.normal, eta, refracted)) {
        ray.direction = reflect(ray.direction, hit.normal);
    } else {
        ray.direction = refracted;
        throughput = throughput * hit.albedo;
    }
    return true;
}

}
