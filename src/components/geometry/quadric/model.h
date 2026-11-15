#pragma once

#include "scene/common.h"
#include <array>
#include <type_traits>

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
namespace hip_demo { struct Ray; struct Hit; struct QuadricRoots; }
#endif

namespace hip_demo::geometry::quadric {
struct Model {
    SceneVector center;
    SceneVector scale{1, 1, 1};
    SceneVector diagonal{1, 1, 1};
    SceneVector cross;
    SceneVector linear;
    float constant = -1;
    bool capped = false;
    Axis axis = Axis::y;

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        return {SceneVector{center.x - scale.x - margin, center.y - scale.y - margin, center.z - scale.z - margin},
                SceneVector{center.x + scale.x + margin, center.y + scale.y + margin, center.z + scale.z + margin}};
    }
#if defined(__HIPCC__)
    __device__ float evaluate(float3 point) const;
    __device__ float3 gradient(float3 point) const;
    __device__ QuadricRoots intersections(const Ray& ray) const;
    __device__ float3 local_point(const Ray& ray, float distance) const;
    __device__ float3 normal_at(const Ray& ray, float distance) const;
    __device__ bool intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const;
    __device__ bool same_shape(const Model& other) const;
#endif
};
static_assert(std::is_trivially_copyable_v<Model>);
}
namespace hip_demo { using GpuQuadric = geometry::quadric::Model; }
