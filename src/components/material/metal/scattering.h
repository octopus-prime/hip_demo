#pragma once

#include "components/material/metal/model.h"
#include "geometry/hit.h"
#include "math/random.h"

namespace hip_demo::material::metal {

__device__ inline float3 Shader::evaluate(const Ray&, const Hit&, float3, float) const {
    return {};
}

__device__ inline bool Shader::scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const {
    const float3 reflected = reflect(ray.direction, hit.normal);
    ray.direction = normalize(reflected + hit.roughness * random_in_unit_sphere(random));
    if (dot(ray.direction, hit.normal) <= 0.0f) {
        return false;
    }
    throughput = throughput * hit.albedo;
    return true;
}

}
