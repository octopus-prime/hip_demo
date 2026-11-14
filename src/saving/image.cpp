#include "saving/image.h"
#include "saving/bmp.h"
#include "saving/png.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace hip_demo {

ImageFormat image_format(const std::string& output_path) {
    auto extension = std::filesystem::path(output_path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".bmp") return ImageFormat::bmp;
    if (extension == ".png") return ImageFormat::png;
    throw std::runtime_error("Unsupported image extension '" + extension +
                             "'; use .bmp or .png: " + output_path);
}

void validate_image(const Image& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::uint64_t>(image.width) * image.height) {
        throw std::runtime_error("Invalid image dimensions or pixel count");
    }
}

void save_image(const Image& image, const std::string& output_path) {
    switch (image_format(output_path)) {
    case ImageFormat::bmp:
        save_bmp(image, output_path);
        break;
    case ImageFormat::png:
        save_png(image, output_path);
        break;
    }
}

}
