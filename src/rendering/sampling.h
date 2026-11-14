#pragma once

#include "math/vector.h"
#include "scene/defaults.h"
#include <cstddef>

namespace hip_demo {

struct AdaptivePixel {
    float3 sum;
    float3 mean;
    float3 m2;
    unsigned int count;
    bool specular;
    bool done;
};

__device__ inline void add_adaptive_sample(AdaptivePixel& pixel, float3 value) {
    ++pixel.count;
    pixel.sum = pixel.sum + value;
    const float3 delta = value - pixel.mean;
    pixel.mean = pixel.mean + delta / static_cast<float>(pixel.count);
    pixel.m2 = pixel.m2 + delta * (value - pixel.mean);
}

__device__ inline bool adaptive_converged(const AdaptivePixel& pixel, const SceneSampling* sampling = nullptr) {
    const unsigned int minimum = sampling ? (pixel.specular ? sampling->specular_minimum : sampling->minimum) :
        (pixel.specular ? adaptive_specular_min_samples : adaptive_min_samples);
    if (pixel.count < minimum || pixel.count < 2) {
        return false;
    }
    const float n = static_cast<float>(pixel.count);
    const float floor = sampling ? sampling->brightness_floor : adaptive_brightness_floor;
    const float error = sampling ? sampling->relative_error : adaptive_relative_error;
    const float3 scale{fmaxf(floor, fabsf(pixel.mean.x)),
                       fmaxf(floor, fabsf(pixel.mean.y)),
                       fmaxf(floor, fabsf(pixel.mean.z))};
    // Welford's M2 gives the squared standard error as M2 / (n * (n - 1)).
    const float multiplier = n * (n - 1.0f) * error * error;
    return isfinite(pixel.m2.x) && isfinite(pixel.m2.y) && isfinite(pixel.m2.z) &&
        pixel.m2.x <= multiplier * scale.x * scale.x &&
        pixel.m2.y <= multiplier * scale.y * scale.y &&
        pixel.m2.z <= multiplier * scale.z * scale.z;
}

struct SampleStatistics {
    unsigned long long total;
    unsigned int minimum;
    unsigned int maximum;
};

__global__ void summarize_samples(const AdaptivePixel* states, std::size_t count, SampleStatistics* statistics) {
    __shared__ unsigned long long totals[256];
    __shared__ unsigned int minimums[256];
    __shared__ unsigned int maximums[256];
    const unsigned int lane = threadIdx.x;
    const std::size_t index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + lane;
    const unsigned int samples = index < count ? states[index].count : 0;
    totals[lane] = samples;
    minimums[lane] = index < count ? samples : 0xffffffffu;
    maximums[lane] = samples;
    __syncthreads();
    for (unsigned int stride = blockDim.x / 2; stride > 0; stride /= 2) {
        if (lane < stride) {
            totals[lane] += totals[lane + stride];
            minimums[lane] = min(minimums[lane], minimums[lane + stride]);
            maximums[lane] = max(maximums[lane], maximums[lane + stride]);
        }
        __syncthreads();
    }
    if (lane == 0) {
        atomicAdd(&statistics->total, totals[0]);
        atomicMin(&statistics->minimum, minimums[0]);
        atomicMax(&statistics->maximum, maximums[0]);
    }
}

}
