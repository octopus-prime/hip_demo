#pragma once

#include "app/options.h"
#include "components/scene/preparation.h"
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
        std::size_t primitives = 0, meshes = 0, texts = 0, csgs = 0;
        const auto count_object = [&](const Object& object) {
            if (std::holds_alternative<Mesh>(object.geometry)) ++meshes;
            else if (std::holds_alternative<ExtrudedText>(object.geometry)) ++texts;
            else if (std::holds_alternative<Csg>(object.geometry)) ++csgs;
            else ++primitives;
        };
        for (const auto& object : description.objects) count_object(object);
        for (const auto& placement : description.instances) count_object(description.prototypes.at(placement.prototype));
        std::cout << "Scene: " << primitives << " primitives, "
                  << meshes << " meshes, " << texts << " text objects, "
                  << csgs << " CSG objects; "
                  << (sampling.adaptive ? "up to " : "") << sampling.maximum
                  << " samples per pixel (" << (sampling.adaptive ? "adaptive" : "fixed") << ").\n";
        std::cout << "Preparing scene and GPU buffers...\n" << std::flush;
        auto prepared = prepare_scene(description);
        const HiprtScene scene(device.id, std::move(prepared), nullptr, 0, render.unified_traversal);
        std::cout << "Traversal: " << (render.unified_traversal ? "unified" : "split") << ".\n";
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
