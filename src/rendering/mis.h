#pragma once

#include <hip/hip_runtime.h>
#include <cmath>

namespace hip_demo {
__device__ inline float power_heuristic(double sampled_pdf, double other_pdf) {
    if (sampled_pdf < 0 || other_pdf < 0 || !isfinite(sampled_pdf) || !isfinite(other_pdf))
        __builtin_trap();
    const double scale = fmax(sampled_pdf, other_pdf);
    if (scale == 0) return 0;
    const double a = sampled_pdf / scale, b = other_pdf / scale;
    return static_cast<float>(a * a / (a * a + b * b));
}
}
