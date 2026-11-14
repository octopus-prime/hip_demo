#pragma once

#include "math/vector.h"
#include <cstdint>

namespace hip_demo {

__device__ inline float noise_lattice(int x, int y) {
    std::uint32_t hash = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
                         static_cast<std::uint32_t>(y) * 0xd8163841u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return static_cast<float>(hash & 0xffffu) / 65535.0f;
}

__device__ inline float value_noise(float x, float y) {
    const int ix = static_cast<int>(floorf(x));
    const int iy = static_cast<int>(floorf(y));
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float lower = noise_lattice(ix, iy) * (1.0f - fx) + noise_lattice(ix + 1, iy) * fx;
    const float upper = noise_lattice(ix, iy + 1) * (1.0f - fx) + noise_lattice(ix + 1, iy + 1) * fx;
    return lower * (1.0f - fy) + upper * fy;
}

__device__ inline float fractal_noise(float x, float y) {
    float value = 0.0f;
    float amplitude = 0.5f;
    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * (2.0f * value_noise(x, y) - 1.0f);
        const float next_x = 1.6f * x + 1.2f * y + 7.1f;
        y = -1.2f * x + 1.6f * y + 19.3f;
        x = next_x;
        amplitude *= 0.5f;
    }
    return value;
}

}
