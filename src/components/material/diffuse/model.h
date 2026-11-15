#pragma once

#include "components/texture/model.h"
#include "shader/traits.h"

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
namespace hip_demo { struct Ray; struct Hit; struct Random; }
#endif

namespace hip_demo::material::diffuse {

struct Model { Texture albedo = ConstantColor{}; };

#if defined(__HIPCC__)
struct Shader {
    static constexpr shader::Traits traits{false, false, false};
    __device__ float3 evaluate(const Ray& ray, const Hit& hit, float3 direction, float cosine) const;
    __device__ float pdf(const Ray& ray, const Hit& hit, float3 direction) const;
    __device__ bool scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const;
};
#endif
}
namespace hip_demo { using Diffuse = hip_demo::material::diffuse::Model; }
