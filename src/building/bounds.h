#pragma once

#include "scene/types.h"
#include <hip/hip_runtime.h>
#include <array>
#include <cmath>

namespace hip_demo {

inline std::array<float3, 2> primitive_bounds(const ScenePrimitive& object, float margin) {
    const float major = object.major_radius, minor = object.minor_radius;
    const float width = (object.sphere ? object.radius : major + minor) + margin;
    const float height = (object.sphere ? object.radius : minor + major * std::abs(object.tilt_sine)) + margin;
    const float depth = (object.sphere ? object.radius : minor + major * std::abs(object.tilt_cosine)) + margin;
    return {float3{object.center.x - width, object.center.y - height, object.center.z - depth},
            float3{object.center.x + width, object.center.y + height, object.center.z + depth}};
}

}
