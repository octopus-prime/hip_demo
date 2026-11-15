#pragma once

#include "scene/common.h"
#include <array>
#include <cmath>

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
#endif

namespace hip_demo { struct Ray; struct Hit; struct PolynomialRoots; struct TorusRoots; }

namespace hip_demo::sphere {

struct Model {
    SceneVector center;
    float radius = 0;

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        const float extent = radius + margin;
        return {SceneVector{center.x - extent, center.y - extent, center.z - extent},
                SceneVector{center.x + extent, center.y + extent, center.z + extent}};
    }
    bool operator==(const Model&) const = default;
#if defined(__HIPCC__)
    __device__ PolynomialRoots intersections(const Ray& ray) const;
    __device__ float3 normal_at(const Ray& ray, float distance) const;
    __device__ bool intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const;
#endif
};
}
namespace hip_demo { using Sphere = sphere::Model; }
