#pragma once

#include <cstdint>

namespace hip_demo {
enum class EmitterKind : std::uint8_t { rectangle, torus, mesh };

struct GpuEmitter {
    EmitterKind kind;
    unsigned int object_id;
    unsigned int first;
    unsigned int count;
    double area;
};

struct EmitterTriangle {
    unsigned int triangle;
    double cumulative_area;
};
}
