#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class MeshUp { y, z };
enum class Material : std::uint8_t { diffuse, metal, glass, phong, emissive };
enum class SurfaceKind { color, tiles, wood, mandelbrot, julia, marble };

struct SceneVector {
    float x = 0, y = 0, z = 0;
};

struct SceneMaterial {
    SceneVector albedo;
    Material kind = Material::diffuse;
    float roughness = 1;
};

struct SceneSurface {
    SceneVector color;
    SurfaceKind kind = SurfaceKind::color;
};

struct SceneSampling {
    int minimum = 64;
    int specular_minimum = 256;
    int maximum = 1024;
    int batch = 8;
    float relative_error = 0.05f;
    float brightness_floor = 0.05f;
    bool adaptive = true;
};

struct SceneLight {
    SceneVector center;
    SceneVector radiance;
    float width = 0, depth = 0;
    float major_radius = 0, minor_radius = 0;
    bool torus = false;
};

struct SceneSettings {
    SceneVector camera_position;
    SceneVector camera_target;
    float half_view_height = 0;
    SceneVector room_size;
    SceneSurface surfaces[5];
    SceneLight light;
    SceneSampling sampling;
};

struct ScenePrimitive {
    SceneVector center;
    SceneMaterial material;
    float radius = 0;
    float major_radius = 0, minor_radius = 0;
    float tilt_sine = 0, tilt_cosine = 1;
    bool sphere = true;
};

enum class SceneMeshKind { ply, cat, crystal };

struct SceneMesh {
    SceneMeshKind kind = SceneMeshKind::ply;
    std::string file;
    MeshUp up = MeshUp::y;
    SceneVector fit;
    SceneVector position;
    SceneMaterial material;
};

struct SceneDescription {
    int width = 0, height = 0;
    std::string output;
    SceneSettings settings;
    std::vector<ScenePrimitive> primitives;
    std::vector<SceneMesh> meshes;
};

SceneDescription parse_scene_description(std::string_view text, const std::string& source = "<scene>");
SceneDescription load_scene_description(const std::string& path);
std::string builtin_scene_path(bool detail);
void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up);
