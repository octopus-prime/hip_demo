#pragma once

#include "shader/diffuse.h"
#include "shader/glass.h"
#include "shader/metal.h"
#include "shader/phong.h"

namespace hip_demo {

__device__ inline float3 evaluate_scattering(const Ray& ray, const Hit& hit, float3 direction, float cosine) {
    return hit.material == Material::phong
        ? evaluate_phong(ray, hit, direction, cosine) : evaluate_diffuse(hit, cosine);
}

__device__ inline bool scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) {
    if (hit.material == Material::metal) {
        return scatter_metal(ray, hit, random, throughput);
    }
    if (hit.material == Material::glass) {
        scatter_glass(ray, hit, random, throughput);
        return true;
    }
    if (hit.material == Material::phong && random.next() < 0.5f) {
        return scatter_phong(ray, hit, random, throughput);
    }
    scatter_diffuse(ray, hit, random, throughput);
    return true;
}

}
