#include "building/scene_description.h"
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

SceneDescription build_scene_description(const scene_ast::Scene& parsed) {
    using namespace scene_ast;
    const SceneDiagnostics errors{parsed.text, parsed.source};
    SceneDescription scene;
    scene.width = parsed.image.width;
    scene.height = parsed.image.height;
    scene.output = parsed.image.output;
    // Both the BMP file format and the kernels use 32-bit sizes/indices.
    const auto row = (static_cast<std::uint64_t>(scene.width) * 3 + 3) & ~3ull;
    if (row * scene.height + 54 > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uint64_t>(scene.width) * scene.height > std::numeric_limits<int>::max()) {
        errors.fail(parsed.image.where, "image dimensions exceed BMP or renderer limits");
    }
    auto& settings = scene.settings;
    settings.sampling = parsed.sampling;
    settings.camera_position = parsed.camera.position;
    settings.camera_target = parsed.camera.target;
    settings.half_view_height = parsed.camera.half_view_height;
    const double dx = static_cast<double>(parsed.camera.position.x) - parsed.camera.target.x;
    const double dz = static_cast<double>(parsed.camera.position.z) - parsed.camera.target.z;
    if (dx * dx + dz * dz < 1e-12) {
        errors.fail(parsed.camera.where, "camera direction must not be zero or parallel to the Y axis");
    }
    settings.room_size = parsed.room.size;
    settings.surfaces[0] = parsed.room.floor;
    settings.surfaces[1] = parsed.room.ceiling;
    settings.surfaces[2] = parsed.room.back;
    settings.surfaces[3] = parsed.room.left;
    settings.surfaces[4] = parsed.room.right;

    std::map<std::string, SceneMaterial> materials;
    for (const auto& material : parsed.materials) {
        if (!materials.emplace(material.name, material.value).second) {
            errors.fail(material.where, "duplicate material '" + material.name + "'");
        }
    }
    const auto material_reference = [&](const auto& object) {
        const auto found = materials.find(object.material);
        if (found == materials.end()) errors.fail(object.where, "unknown material '" + object.material + "'");
        return found->second;
    };
    const auto check_radii = [&](const auto& torus) {
        if (torus.minor_radius >= torus.major_radius) {
            errors.fail(torus.where, "minor_radius must be less than major_radius");
        }
    };
    auto& light = settings.light;
    std::visit([&](const auto& source) {
        light.radiance = source.radiance;
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>, RectangleLight>) {
            light.center = {source.center[0], settings.room_size.y, source.center[1]};
            light.width = source.size[0];
            light.depth = source.size[1];
            if (light.width <= 0 || light.depth <= 0) errors.fail(source.where, "light dimensions must be positive");
            if (std::abs(light.center.x) + light.width / 2 > settings.room_size.x / 2 ||
                std::abs(light.center.z) + light.depth / 2 > settings.room_size.z / 2) {
                errors.fail(source.where, "ceiling light must fit inside the room");
            }
        } else {
            check_radii(source);
            light.torus = true;
            light.center = source.center;
            light.major_radius = source.major_radius;
            light.minor_radius = source.minor_radius;
        }
    }, parsed.light);
    for (const auto& object : parsed.objects) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Sphere> || std::is_same_v<T, Torus>) {
                ScenePrimitive primitive;
                primitive.center = value.center;
                primitive.material = material_reference(value);
                if constexpr (std::is_same_v<T, Sphere>) {
                    primitive.radius = value.radius;
                } else {
                    check_radii(value);
                    primitive.sphere = false;
                    primitive.major_radius = value.major_radius;
                    primitive.minor_radius = value.minor_radius;
                    const double angle = static_cast<double>(value.tilt_x) * 3.141592653589793 / 180;
                    primitive.tilt_sine = static_cast<float>(std::sin(angle));
                    primitive.tilt_cosine = static_cast<float>(std::cos(angle));
                }
                scene.primitives.push_back(primitive);
            } else if constexpr (std::is_same_v<T, Text>) {
                scene.texts.push_back({value.font, value.value, value.size, value.depth,
                                       value.position, material_reference(value)});
            } else {
                SceneMesh mesh;
                if constexpr (std::is_same_v<T, Mesh>) {
                    mesh.file = value.file;
                    mesh.up = value.up;
                    mesh.fit = value.fit;
                    mesh.position = value.position;
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
    for (auto& text_object : scene.texts) {
        text_object.font = (directory / std::filesystem::path(text_object.font)).lexically_normal().string();
    }
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
