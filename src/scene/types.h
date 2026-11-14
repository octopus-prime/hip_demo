#pragma once

#include <cstdint>
#include <string>
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

enum class CsgOperation { unite, intersection, difference };
inline constexpr std::uint32_t csg_inside = ~std::uint32_t{0};
inline constexpr std::uint32_t csg_outside = csg_inside - 1;

struct SceneCsgLeaf {
    ScenePrimitive shape;
    std::uint32_t when_inside = csg_inside, when_outside = csg_outside;
    std::uint32_t text_index = 0, first_glyph = 0, glyph_count = 0;
    bool text = false;
};

struct SceneCsg {
    std::uint32_t first_leaf = 0, leaf_count = 0, entry = 0;
    SceneMaterial material;
};

struct SceneMesh {
    SceneMeshKind kind = SceneMeshKind::ply;
    std::string file;
    MeshUp up = MeshUp::y;
    SceneVector fit;
    SceneVector position;
    SceneMaterial material;
};

struct SceneText {
    std::string font, value;
    float size = 1, depth = 0.2f;
    SceneVector position;
    SceneMaterial material;
};

struct SceneDescription {
    SceneSettings settings;
    std::vector<ScenePrimitive> primitives;
    std::vector<SceneMesh> meshes;
    std::vector<SceneText> texts;
    std::vector<SceneCsg> csgs;
    std::vector<SceneCsgLeaf> csg_leaves;
    std::vector<SceneText> csg_texts;
};
