#pragma once

#include "surface/hit.h"
#include "math/random.h"

namespace hip_demo {

__device__ inline bool scatter_metal(Ray& ray, const Hit& hit, Random& random, float3& throughput) {
    const float3 reflected = reflect(ray.direction, hit.normal);
    ray.direction = normalize(reflected + hit.roughness * random_in_unit_sphere(random));
    if (dot(ray.direction, hit.normal) <= 0.0f) {
        return false;
    }
    throughput = throughput * hit.albedo;
    return true;
}

}
