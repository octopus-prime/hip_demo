#include "saving/png.h"

#include <boost/gil.hpp>
#include <boost/gil/extension/io/png.hpp>
#include <fstream>
#include <stdexcept>

namespace hip_demo {

void save_png(const Image& image, const std::string& output_path) {
    validate_image(image);
    boost::gil::rgb8_image_t rgb(image.width, image.height);
    const auto pixels = boost::gil::view(rgb);
    for (int y = 0; y < image.height; ++y) {
        auto row = pixels.row_begin(y);
        for (int x = 0; x < image.width; ++x) {
            const auto pixel = image.pixels[x + static_cast<std::size_t>(y) * image.width];
            row[x] = boost::gil::rgb8_pixel_t(
                static_cast<unsigned char>(pixel & 0xff),
                static_cast<unsigned char>((pixel >> 8) & 0xff),
                static_cast<unsigned char>((pixel >> 16) & 0xff));
        }
    }
    std::ofstream output(output_path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not open output image: " + output_path);
    try {
        output.exceptions(std::ios::failbit | std::ios::badbit);
        boost::gil::write_view(output, pixels, boost::gil::png_tag{});
        output.close();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error("Failed while writing output image: " + output_path + ": " + error.what());
    }
}

}
