#pragma once

#include "components/texture/model.h"
#include "shader/traits.h"

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
namespace hip_demo { struct Ray; struct Hit; struct Random; }
#endif

namespace hip_demo::metal {

struct Model { Texture albedo = ConstantColor{}; float roughness = 1; };

#if defined(__HIPCC__)
struct Shader {
    static constexpr shader::Traits traits{true, false, true};
    __device__ float3 evaluate(const Ray& ray, const Hit& hit, float3 direction, float cosine) const;
    __device__ bool scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const;
};
#endif
}
namespace hip_demo { using Metal = metal::Model; }
