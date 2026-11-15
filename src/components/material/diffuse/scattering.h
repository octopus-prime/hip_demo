#pragma once

#include "components/material/diffuse/model.h"
#include "geometry/hit.h"
#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo::material::diffuse {

__device__ inline float3 sample_diffuse_direction(float3 normal, Random& random) {
    return normalize(normal + random_unit_vector(random));
}

__device__ inline float3 Shader::evaluate(const Ray&, const Hit& hit, float3, float cosine) const {
    return hit.albedo * (cosine / pi);
}

__device__ inline float Shader::pdf(const Ray&, const Hit& hit, float3 direction) const {
    return fmaxf(0, dot(hit.normal, direction)) / pi;
}

__device__ inline bool Shader::scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const {
    ray.direction = sample_diffuse_direction(hit.normal, random);
    throughput = throughput * hit.albedo;
    return true;
}

}
