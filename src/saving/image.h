#pragma once

#include <cstdint>
#include <vector>

namespace hip_demo {

struct Image {
    int width, height;
    // Top-down rows, with red in bits 0..7, green in 8..15, and blue in 16..23.
    std::vector<std::uint32_t> pixels;
};

}
