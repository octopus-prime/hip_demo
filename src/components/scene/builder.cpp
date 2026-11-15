#include "components/scene/builder.h"
#include "components/camera/builder.h"
#include "components/room/builder.h"
#include "components/material/builder.h"
#include "components/light/builder.h"
#include "components/object/builder.h"
#include "parsing/diagnostics.h"
#include "parsing/parser.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hip_demo::scene {

Model build(const Description& parsed) {
    const SceneDiagnostics errors{parsed.text, parsed.source};
    const hip_demo::SceneValues values{errors};
    SceneDescription scene;
    auto& settings = scene.settings;
    settings.camera = camera::build(parsed.camera, values);
    settings.room = room::build(parsed.room, values);

    std::map<std::string, hip_demo::Material> materials;
    for (const auto& material : parsed.materials) {
        if (!materials.emplace(material.name, hip_demo::material::build(material.value, material.where, values)).second) {
            errors.fail(material.where, "duplicate material '" + material.name + "'");
        }
    }
    settings.light = light::build(parsed.light, settings.room, values);
    for (const auto& object : parsed.objects)
        scene.objects.push_back(hip_demo::object::build(object, materials, values));
    if (const auto* light = std::get_if<hip_demo::light::torus::Model>(&settings.light)) {
        scene.objects.push_back({hip_demo::Torus{light->center, light->major_radius, light->minor_radius},
                                 hip_demo::Emissive{hip_demo::ConstantColor{{1, 1, 1}}}});
    }
    return scene;
}

Model load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open scene: " + path);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) throw std::runtime_error("Failed while reading scene: " + path);
    auto scene = parse_scene_description(text, path);
    const auto directory = std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    const auto resolve_text = [&](hip_demo::ExtrudedText& text) {
        text.font = (directory / std::filesystem::path(text.font)).lexically_normal().string();
    };
    const auto resolve_csg = [&](auto&& self, hip_demo::CsgNode& node) -> void {
        if (auto* text = std::get_if<hip_demo::ExtrudedText>(&node.geometry)) resolve_text(*text);
        for (auto& child : node.children) self(self, child);
    };
    for (auto& object : scene.objects) {
        std::visit([&](auto& geometry) {
            using T = std::decay_t<decltype(geometry)>;
            if constexpr (std::is_same_v<T, hip_demo::Mesh>) {
                if (geometry.kind == SceneMeshKind::ply)
                    geometry.file = (directory / std::filesystem::path(geometry.file)).lexically_normal().string();
            } else if constexpr (std::is_same_v<T, hip_demo::ExtrudedText>) resolve_text(geometry);
            else if constexpr (std::is_same_v<T, hip_demo::Csg>) resolve_csg(resolve_csg, geometry.root);
        }, object.geometry);
    }
    return scene;
}

std::string builtin_path(bool detail) {
    return (std::filesystem::path(HIP_DEMO_SCENE_DIRECTORY) /
            (detail ? "detail.hipscene" : "classic.hipscene")).string();
}

void override_mesh(Model& scene, const std::string& path, MeshUp up) {
    hip_demo::Object* selected = nullptr;
    for (auto& object : scene.objects) {
        const auto* mesh = std::get_if<hip_demo::Mesh>(&object.geometry);
        if (mesh && (mesh->kind == SceneMeshKind::cat || mesh->kind == SceneMeshKind::ply)) {
            if (selected) throw std::runtime_error("--mesh requires a scene with exactly one cat or PLY mesh.");
            selected = &object;
        }
    }
    if (!selected) throw std::runtime_error("--mesh requires a scene containing a cat or PLY mesh.");
    auto& mesh = std::get<hip_demo::Mesh>(selected->geometry);
    if (mesh.kind == SceneMeshKind::cat) {
        mesh.fit = {1.05f, 1.55f, 0.9f};
        mesh.position = {1.85f, 0.02f, 1.75f};
        selected->material = hip_demo::Diffuse{hip_demo::ConstantColor{{0.78f, 0.36f, 0.16f}}};
    }
    mesh.kind = SceneMeshKind::ply;
    mesh.file = path;
    mesh.up = up;
}

}
