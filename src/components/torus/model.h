#pragma once

#include "scene/common.h"
#include <array>
#include <cmath>

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
#endif

namespace hip_demo { struct Ray; struct Hit; struct PolynomialRoots; struct TorusRoots; }

namespace hip_demo::torus {

struct Model {
    SceneVector center;
    float major_radius = 0, minor_radius = 0;
    float tilt_sine = 0, tilt_cosine = 1;

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        const float width = major_radius + minor_radius + margin;
        const float height = minor_radius + major_radius * std::abs(tilt_sine) + margin;
        const float depth = minor_radius + major_radius * std::abs(tilt_cosine) + margin;
        return {SceneVector{center.x - width, center.y - height, center.z - depth},
                SceneVector{center.x + width, center.y + height, center.z + depth}};
    }
    bool operator==(const Model&) const = default;
#if defined(__HIPCC__)
    __device__ Ray local_ray(const Ray& ray) const;
    __device__ TorusRoots intersections(const Ray& ray, float minimum, float maximum) const;
    __device__ bool intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const;
#endif
};
}
namespace hip_demo { using Torus = torus::Model; }
