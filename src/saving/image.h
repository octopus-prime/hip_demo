#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hip_demo {

struct Image {
    int width, height;
    // Top-down rows, with red in bits 0..7, green in 8..15, and blue in 16..23.
    std::vector<std::uint32_t> pixels;
};

enum class ImageFormat { bmp, png };

ImageFormat image_format(const std::string& output_path);
void validate_image(const Image& image);
void save_image(const Image& image, const std::string& output_path);

}
