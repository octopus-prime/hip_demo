#pragma once

#include "math/vector.h"
#include <cstdint>

namespace hip_demo {

struct Random {
    std::uint32_t state;

    __device__ inline float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

__device__ inline float3 random_in_unit_sphere(Random& random) {
    while (true) {
        const float3 point{
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
        };
        if (dot(point, point) < 1.0f) {
            return point;
        }
    }
}

__device__ inline float3 random_unit_vector(Random& random) {
    return normalize(random_in_unit_sphere(random));
}

}
