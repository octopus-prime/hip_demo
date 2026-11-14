#pragma once

#include "app/options.h"
#include "building/geometry.h"
#include "gpu/device.h"
#include "gpu/download.h"
#include "gpu/scene.h"
#include "saving/image.h"
#include <iostream>
#include <utility>

namespace hip_demo {

inline int run_demo(int argc, char** argv) {
    try {
        const auto description = load_options(argc, argv);
        const auto format = image_format(description.output);
        const auto device = select_device();
        const auto& sampling = description.settings.sampling;
        std::cout << "Rendering on " << device.properties.name << " ("
                  << device.properties.gcnArchName << ")...\n";
        std::cout << "Scene: " << description.primitives.size() << " primitives, "
                  << description.meshes.size() << " meshes, " << description.texts.size() << " text objects; "
                  << (sampling.adaptive ? "up to " : "") << sampling.maximum
                  << " samples per pixel (" << (sampling.adaptive ? "adaptive" : "fixed") << ").\n";
        std::cout << "Preparing scene and GPU buffers...\n" << std::flush;
        auto prepared = prepare_scene(description);
        const HiprtScene scene(device.id, std::move(prepared));
        const auto rendered = render_image(scene.scene(), description);
        std::cout << "Copying image to CPU and writing "
                  << (format == ImageFormat::png ? "PNG" : "BMP") << "...\n" << std::flush;
        const auto image = download_image(rendered);
        save_image(image, description.output);
        std::cout << "Saved " << description.output << " (" << description.width << 'x' << description.height
                  << ", " << (sampling.adaptive ? "up to " : "") << sampling.maximum
                  << " samples per pixel).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nHIP RT demo failed: " << error.what() << '\n';
        return 1;
    }
}

}
