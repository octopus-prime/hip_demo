#include "app/options.h"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace hip_demo {

SceneDescription load_options(int argc, char** argv) {
    std::string output_path;
    int sample_count = 0;
    bool adaptive = true;
    std::string ply_path;
    MeshUp mesh_up = MeshUp::y;
    bool mesh_up_specified = false;
    bool scene_specified = false;
    std::string scene_path = builtin_scene_path(false);
    bool fixed_specified = false;
    int positional_count = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view input(argv[index]);
        if (input == "--fixed") {
            adaptive = false;
            fixed_specified = true;
        } else if (input == "--scene") {
            if (scene_specified || index + 1 >= argc) {
                throw std::runtime_error("--scene requires one scene file or preset name.");
            }
            const std::string_view name(argv[++index]);
            if (name == "classic") {
                scene_path = builtin_scene_path(false);
            } else if (name == "detail") {
                scene_path = builtin_scene_path(true);
            } else {
                scene_path = name;
            }
            scene_specified = true;
        } else if (input == "--mesh") {
            if (!ply_path.empty() || index + 1 >= argc || std::string_view(argv[index + 1]).substr(0, 2) == "--" ||
                std::string_view(argv[index + 1]).empty()) {
                throw std::runtime_error("--mesh requires exactly one PLY file path.");
            }
            ply_path = argv[++index];
        } else if (input == "--mesh-up") {
            if (mesh_up_specified || index + 1 >= argc) {
                throw std::runtime_error("--mesh-up requires one axis: y or z.");
            }
            const std::string_view axis(argv[++index]);
            if (axis == "y") {
                mesh_up = MeshUp::y;
            } else if (axis == "z") {
                mesh_up = MeshUp::z;
            } else {
                throw std::runtime_error("--mesh-up requires one axis: y or z.");
            }
            mesh_up_specified = true;
        } else if (input.substr(0, 2) == "--") {
            throw std::runtime_error("Unknown option: " + std::string(input));
        } else if (positional_count == 0) {
            output_path = input;
            ++positional_count;
        } else if (positional_count == 1) {
            const auto result = std::from_chars(input.data(), input.data() + input.size(), sample_count);
            if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || sample_count <= 0) {
                throw std::runtime_error("Samples per pixel must be a positive integer.");
            }
            ++positional_count;
        } else {
            throw std::runtime_error(
                "Usage: renderer [output.bmp|output.png] [samples-per-pixel] [--fixed] [--scene file|classic|detail] "
                "[--mesh file.ply] [--mesh-up y|z]");
        }
    }
    if (mesh_up_specified && ply_path.empty()) {
        throw std::runtime_error("--mesh-up requires --mesh.");
    }
    auto description = load_scene_description(scene_path);
    if (!output_path.empty()) description.output = output_path;
    if (positional_count > 1) description.settings.sampling.maximum = sample_count;
    if (fixed_specified) description.settings.sampling.adaptive = adaptive;
    if (!ply_path.empty()) override_scene_mesh(description, ply_path, mesh_up);
    return description;
}

}
