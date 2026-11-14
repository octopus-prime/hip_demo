#include "building/scene_description.h"
#include "building/csg.h"
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
    settings.surfaces[0] = values.surface(parsed.room.floor, parsed.room.where);
    settings.surfaces[1] = values.surface(parsed.room.ceiling, parsed.room.where);
    settings.surfaces[2] = values.surface(parsed.room.back, parsed.room.where);
    settings.surfaces[3] = values.surface(parsed.room.left, parsed.room.where);
    settings.surfaces[4] = values.surface(parsed.room.right, parsed.room.where);

    std::map<std::string, SceneMaterial> materials;
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
            if constexpr (std::is_same_v<T, Sphere> || std::is_same_v<T, Torus>) {
                auto primitive = values.primitive(value);
                primitive.material = material_reference(value);
                scene.primitives.push_back(primitive);
            } else if constexpr (std::is_same_v<T, Csg>) {
                hip_demo::append_csg(scene, value, material_reference(value), errors);
            } else if constexpr (std::is_same_v<T, Text>) {
                auto text = values.text(value);
                text.material = material_reference(value);
                scene.texts.push_back(std::move(text));
            } else {
                SceneMesh mesh;
                if constexpr (std::is_same_v<T, Mesh>) {
                    mesh.file = values.string(value.file, value.where);
                    mesh.up = value.up;
                    mesh.fit = values.dimensions(value.fit, value.where);
                    mesh.position = values.vector(value.position, value.where);
                    mesh.material = material_reference(value);
                } else {
                    mesh.kind = value;
                }
                scene.meshes.push_back(mesh);
            }
        }, object);
    }
    if (light.torus) {
        ScenePrimitive emitter;
        emitter.sphere = false;
        emitter.center = light.center;
        emitter.major_radius = light.major_radius;
        emitter.minor_radius = light.minor_radius;
        emitter.material = {{1, 1, 1}, Material::emissive, 0};
        scene.primitives.push_back(emitter);
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
    for (auto& mesh : scene.meshes) {
        if (mesh.kind == SceneMeshKind::ply) {
            mesh.file = (directory / std::filesystem::path(mesh.file)).lexically_normal().string();
        }
    }
    const auto resolve_fonts = [&](auto& texts) {
        for (auto& text : texts)
            text.font = (directory / std::filesystem::path(text.font)).lexically_normal().string();
    };
    resolve_fonts(scene.texts);
    resolve_fonts(scene.csg_texts);
    return scene;
}

std::string builtin_scene_path(bool detail) {
    return (std::filesystem::path(HIP_DEMO_SCENE_DIRECTORY) /
            (detail ? "detail.hipscene" : "classic.hipscene")).string();
}

void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up) {
    SceneMesh* selected = nullptr;
    for (auto& mesh : scene.meshes) {
        if (mesh.kind == SceneMeshKind::cat || mesh.kind == SceneMeshKind::ply) {
            if (selected) throw std::runtime_error("--mesh requires a scene with exactly one cat or PLY mesh.");
            selected = &mesh;
        }
    }
    if (!selected) throw std::runtime_error("--mesh requires a scene containing a cat or PLY mesh.");
    if (selected->kind == SceneMeshKind::cat) {
        selected->fit = {1.05f, 1.55f, 0.9f};
        selected->position = {1.85f, 0.02f, 1.75f};
        selected->material = {{0.78f, 0.36f, 0.16f}, Material::diffuse, 1};
    }
    selected->kind = SceneMeshKind::ply;
    selected->file = path;
    selected->up = up;
}
