#include "building/scene_description.h"
#include "building/scene_values.h"
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

SceneDescription build_scene_description(const scene_ast::Scene& parsed) {
    using namespace scene_ast;
    const SceneDiagnostics errors{parsed.text, parsed.source};
    const hip_demo::SceneValues values{errors};
    SceneDescription scene;
    auto& settings = scene.settings;
    settings.camera_position = values.vector(parsed.camera.position, parsed.camera.where);
    settings.camera_target = values.vector(parsed.camera.target, parsed.camera.where);
    settings.half_view_height = values.positive(parsed.camera.half_view_height, parsed.camera.where);
    const double dx = static_cast<double>(settings.camera_position.x) - settings.camera_target.x;
    const double dz = static_cast<double>(settings.camera_position.z) - settings.camera_target.z;
    if (dx * dx + dz * dz < 1e-12) {
        errors.fail(parsed.camera.where, "camera direction must not be zero or parallel to the Y axis");
    }
    settings.room_size = values.dimensions(parsed.room.size, parsed.room.where);
    settings.textures[0] = values.texture(parsed.room.floor, parsed.room.where);
    settings.textures[1] = values.texture(parsed.room.ceiling, parsed.room.where);
    settings.textures[2] = values.texture(parsed.room.back, parsed.room.where);
    settings.textures[3] = values.texture(parsed.room.left, parsed.room.where);
    settings.textures[4] = values.texture(parsed.room.right, parsed.room.where);

    std::map<std::string, hip_demo::Material> materials;
    for (const auto& material : parsed.materials) {
        if (!materials.emplace(material.name, values.material(material.value, material.where)).second) {
            errors.fail(material.where, "duplicate material '" + material.name + "'");
        }
    }
    const auto material_reference = [&](const auto& object) {
        const auto found = materials.find(object.material);
        if (found == materials.end()) errors.fail(object.where, "unknown material '" + object.material + "'");
        return found->second;
    };
    auto& light = settings.light;
    std::visit([&](const auto& source) {
        light.radiance = values.radiance(source.radiance, source.where);
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>, RectangleLight>) {
            light.center = {values.scalar(source.center[0], source.where), settings.room_size.y,
                            values.scalar(source.center[1], source.where)};
            light.width = values.positive(source.size[0], source.where);
            light.depth = values.positive(source.size[1], source.where);
            if (std::abs(light.center.x) + light.width / 2 > settings.room_size.x / 2 ||
                std::abs(light.center.z) + light.depth / 2 > settings.room_size.z / 2) {
                errors.fail(source.where, "ceiling light must fit inside the room");
            }
        } else {
            const auto sizes = values.radii(source.major_radius, source.minor_radius, source.where);
            light.torus = true;
            light.center = values.vector(source.center, source.where);
            light.major_radius = sizes[0];
            light.minor_radius = sizes[1];
        }
    }, parsed.light);
    for (const auto& object : parsed.objects) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Sphere> || std::is_same_v<T, Torus> ||
                          std::is_same_v<T, Csg> || std::is_same_v<T, Text>) {
                scene.objects.push_back({values.geometry(value), material_reference(value)});
            } else {
                hip_demo::Mesh mesh;
                hip_demo::Material material;
                if constexpr (std::is_same_v<T, Mesh>) {
                    mesh.file = values.string(value.file, value.where);
                    mesh.up = value.up;
                    mesh.fit = values.dimensions(value.fit, value.where);
                    mesh.position = values.vector(value.position, value.where);
                    material = material_reference(value);
                } else {
                    mesh.kind = value;
                    material = value == SceneMeshKind::crystal
                        ? hip_demo::Material{hip_demo::Phong{hip_demo::ConstantColor{{0.10f, 0.65f, 0.72f}}}}
                        : hip_demo::Material{hip_demo::Diffuse{hip_demo::ConstantColor{{0.78f, 0.36f, 0.16f}}}};
                }
                scene.objects.push_back({std::move(mesh), std::move(material)});
            }
        }, object);
    }
    if (light.torus) {
        scene.objects.push_back({hip_demo::Torus{light.center, light.major_radius, light.minor_radius},
                                 hip_demo::Emissive{hip_demo::ConstantColor{{1, 1, 1}}}});
    }
    return scene;
}

SceneDescription load_scene_description(const std::string& path) {
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

std::string builtin_scene_path(bool detail) {
    return (std::filesystem::path(HIP_DEMO_SCENE_DIRECTORY) /
            (detail ? "detail.hipscene" : "classic.hipscene")).string();
}

void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up) {
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
