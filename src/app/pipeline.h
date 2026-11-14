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
        const auto options = load_options(argc, argv);
        if (options.help) {
            std::cout << options.help_text;
            return 0;
        }
        if (!options.save_config_path.empty()) {
            save_render_config(options.render, options.save_config_path);
            std::cout << "Saved render config " << options.save_config_path << '\n';
            return 0;
        }
        const auto& render = options.render;
        const auto description = load_scene_description(
            options.scene == "classic" || options.scene == "detail"
                ? builtin_scene_path(options.scene == "detail") : options.scene);
        const auto format = image_format(render.output);
        const auto device = select_device(render.device);
        const auto& sampling = render.sampling;
        std::cout << "Rendering on " << device.properties.name << " ("
                  << device.properties.gcnArchName << ")...\n";
        std::cout << "Scene: " << description.primitives.size() << " primitives, "
                  << description.meshes.size() << " meshes, " << description.texts.size() << " text objects; "
                  << (sampling.adaptive ? "up to " : "") << sampling.maximum
                  << " samples per pixel (" << (sampling.adaptive ? "adaptive" : "fixed") << ").\n";
        std::cout << "Preparing scene and GPU buffers...\n" << std::flush;
        auto prepared = prepare_scene(description);
        const HiprtScene scene(device.id, std::move(prepared));
        const auto rendered = render_image(scene.scene(), render);
        std::cout << "Copying image to CPU and writing "
                  << (format == ImageFormat::png ? "PNG" : "BMP") << "...\n" << std::flush;
        const auto image = download_image(rendered);
        save_image(image, render.output);
        std::cout << "Saved " << render.output << " (" << render.width << 'x' << render.height
                  << ", " << (sampling.adaptive ? "up to " : "") << sampling.maximum
                  << " samples per pixel).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nHIP RT demo failed: " << error.what() << '\n';
        return 1;
    }
}

}
