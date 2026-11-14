#pragma once

#include "rendering/renderer.h"
#include "saving/image.h"

namespace hip_demo {

inline Image download_image(const GpuImage& image) {
    Image result{image.width, image.height,
                 std::vector<std::uint32_t>(static_cast<std::size_t>(image.width) * image.height)};
    check_hip(hipMemcpy(result.pixels.data(), image.pixels.get(),
                        result.pixels.size() * sizeof(std::uint32_t), hipMemcpyDeviceToHost),
              "Copying the rendered image to the CPU");
    return result;
}

}
