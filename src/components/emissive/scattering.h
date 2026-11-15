#pragma once

#include "components/emissive/model.h"
#include "geometry/hit.h"

namespace hip_demo::emissive {

__device__ inline float3 Shader::evaluate(const Ray&, const Hit&, float3, float) const {
    return {};
}

__device__ inline bool Shader::scatter(Ray&, const Hit&, Random&, float3&) const {
    return false;
}

}
