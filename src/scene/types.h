#pragma once

#include "geometry/analytic.h"
#include <array>
#include <string>
#include <variant>
#include <vector>

namespace hip_demo {

struct ConstantColor { SceneVector color; };
struct Tiles {};
struct Wood {};
struct Mandelbrot {};
struct Julia {};
struct Marble {};
using Texture = std::variant<ConstantColor, Tiles, Wood, Mandelbrot, Julia, Marble>;

struct Diffuse { Texture albedo = ConstantColor{}; };
struct Phong { Texture albedo = ConstantColor{}; };
struct Metal { Texture albedo = ConstantColor{}; float roughness = 1; };
struct Glass { Texture albedo = ConstantColor{}; };
struct Emissive { Texture albedo = ConstantColor{}; };
using Material = std::variant<Diffuse, Phong, Metal, Glass, Emissive>;

struct Mesh {
    SceneMeshKind kind = SceneMeshKind::ply;
    std::string file;
    MeshUp up = MeshUp::y;
    SceneVector fit;
    SceneVector position;
};

struct ExtrudedText {
    std::string font, value;
    float size = 1, depth = 0.2f;
    SceneVector position;
    bool operator==(const ExtrudedText&) const = default;
};

struct CsgNode {
    std::variant<Sphere, Torus, ExtrudedText> geometry;
    CsgOperation operation = CsgOperation::unite;
    std::vector<CsgNode> children;
};
struct Csg { CsgNode root; };

using Geometry = std::variant<Sphere, Torus, Mesh, ExtrudedText, Csg>;
struct Object {
    Geometry geometry;
    Material material;
};

struct SceneSettings {
    SceneVector camera_position;
    SceneVector camera_target;
    float half_view_height = 0;
    SceneVector room_size;
    std::array<Texture, 5> textures;
    SceneLight light;
};

}

struct SceneDescription {
    hip_demo::SceneSettings settings;
    std::vector<hip_demo::Object> objects;
};
