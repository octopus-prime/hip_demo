#pragma once

#include "texture/fractal.h"

namespace hip_demo {

__device__ inline float3 mandelbrot_albedo(float3 point, float depth, float height) {
    return fractal_wall_albedo(point, false, depth, height);
}

}
