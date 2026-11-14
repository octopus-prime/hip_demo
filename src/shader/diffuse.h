#pragma once

#include "surface/hit.h"
#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline float3 evaluate_diffuse(const Hit& hit, float cosine) {
    return hit.albedo * (cosine / pi);
}

__device__ inline void scatter_diffuse(Ray& ray, const Hit& hit, Random& random, float3& throughput) {
    ray.direction = normalize(hit.normal + random_unit_vector(random));
    throughput = throughput * hit.albedo;
}

}
