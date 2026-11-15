#pragma once

#include "shader/visit.h"
#include "components/diffuse/scattering.h"
#include "components/glass/scattering.h"
#include "components/metal/scattering.h"
#include "components/phong/scattering.h"
#include "components/emissive/scattering.h"

namespace hip_demo {

__device__ inline float3 evaluate_scattering(const Ray& ray, const Hit& hit, float3 direction, float cosine) {
    return shader::visit([&] __device__ (auto value) {
        return value.evaluate(ray, hit, direction, cosine);
    }, hit.material);
}

__device__ inline bool scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) {
    return shader::visit([&] __device__ (auto value) {
        return value.scatter(ray, hit, random, throughput);
    }, hit.material);
}

}
