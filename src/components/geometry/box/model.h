#pragma once

#include "scene/common.h"
#include <array>

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
namespace hip_demo { struct Ray; struct Hit; struct BoxIntersections; }
#endif

namespace hip_demo::geometry::box {
struct Model {
    SceneVector center;
    SceneVector size{2, 2, 2};

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        return {SceneVector{center.x - size.x / 2 - margin, center.y - size.y / 2 - margin,
                            center.z - size.z / 2 - margin},
                SceneVector{center.x + size.x / 2 + margin, center.y + size.y / 2 + margin,
                            center.z + size.z / 2 + margin}};
    }
#if defined(__HIPCC__)
    __device__ BoxIntersections intersections(const Ray& ray, bool solid = false) const;
    __device__ bool intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const;
    __device__ bool same_shape(const Model& other) const;
#endif
};
}
namespace hip_demo { using Box = geometry::box::Model; }
