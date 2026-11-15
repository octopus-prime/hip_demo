#pragma once

#include "gpu/types.h"
#include <hip/hip_runtime.h>
#include <array>
#include <cmath>

namespace hip_demo {

inline std::array<float3, 2> primitive_bounds(const GpuGeometry& geometry, float margin) {
    const auto box = geometry.bounds(margin);
    return {float3{box[0].x, box[0].y, box[0].z}, float3{box[1].x, box[1].y, box[1].z}};
}

}
