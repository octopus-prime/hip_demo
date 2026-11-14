#include "saving/bmp.h"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace hip_demo {

void save_bmp(const Image& data, const std::string& output_path) {
    const auto& pixels = data.pixels;
    const int image_width = data.width;
    const int image_height = data.height;
    constexpr std::uint32_t header_size = 54;
    const std::uint64_t row_bytes = (static_cast<std::uint64_t>(image_width) * 3 + 3) & ~3ull;
    if (image_width <= 0 || image_height <= 0 ||
        pixels.size() != static_cast<std::uint64_t>(image_width) * image_height ||
        row_bytes * image_height > std::numeric_limits<std::uint32_t>::max() - header_size) {
        throw std::runtime_error("Invalid image dimensions or pixel count for BMP: " + output_path);
    }
    std::ofstream image(output_path, std::ios::binary);
    if (!image) {
        throw std::runtime_error("Could not open output image: " + output_path);
    }
    const auto row_size = static_cast<std::uint32_t>(row_bytes);
    std::array<unsigned char, header_size> header{};
    const auto write_header_value = [&header](std::size_t offset, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            header[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    header[0] = 'B';
    header[1] = 'M';
    write_header_value(2, header_size + row_size * image_height, 4);
    write_header_value(10, header_size, 4);
    write_header_value(14, 40, 4);
    write_header_value(18, image_width, 4);
    write_header_value(22, static_cast<std::uint32_t>(-image_height), 4);
    write_header_value(26, 1, 2);
    write_header_value(28, 24, 2);
    write_header_value(34, row_size * image_height, 4);
    image.write(reinterpret_cast<const char*>(header.data()), header.size());
    constexpr std::array<char, 3> padding{};
    for (int y = 0; y < image_height; ++y) {
        for (int x = 0; x < image_width; ++x) {
            const std::uint32_t pixel = pixels[x + static_cast<std::size_t>(y) * image_width];
            const std::array<unsigned char, 3> color{
                static_cast<unsigned char>((pixel >> 16) & 0xff),
                static_cast<unsigned char>((pixel >> 8) & 0xff),
                static_cast<unsigned char>(pixel & 0xff),
            };
            image.write(reinterpret_cast<const char*>(color.data()), color.size());
        }
        image.write(padding.data(), row_size - image_width * 3);
    }
    image.close();
    if (!image) {
        throw std::runtime_error("Failed while writing output image: " + output_path);
    }
}

}
