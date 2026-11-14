#pragma once

#include "scene/types.h"

#include <array>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace scene_ast {

// Byte offsets into Scene::text remain valid when the AST is copied or moved.
using Position = std::size_t;

struct Vector {
    double x = 0, y = 0, z = 0;
};

struct MaterialValue {
    Vector albedo;
    Material kind = Material::diffuse;
    double roughness = 1;
};

struct Surface {
    Vector color;
    SurfaceKind kind = SurfaceKind::color;
};

struct Camera {
    Position where;
    Vector position, target;
    double half_view_height;
};

struct Room {
    Position where;
    Vector size;
    Surface floor, ceiling, back, left, right;
};

struct NamedMaterial {
    Position where;
    std::string name;
    MaterialValue value;
};

struct RectangleLight {
    Position where;
    std::array<double, 2> center, size;
    Vector radiance;
};

struct TorusLight {
    Position where;
    Vector center;
    double major_radius, minor_radius;
    Vector radiance;
};

struct Sphere {
    Position where;
    Vector center;
    double radius;
    std::string material;
};

struct Torus {
    Position where;
    Vector center;
    double major_radius, minor_radius, tilt_x;
    std::string material;
};

struct Mesh {
    Position where;
    std::string file;
    MeshUp up;
    Vector fit, position;
    std::string material;
};

struct Text {
    Position where;
    std::string font, value;
    double size, depth;
    Vector position;
    std::string material;
};

using Light = std::variant<RectangleLight, TorusLight>;
struct CsgNode {
    Position where;
    std::variant<Sphere, Torus> shape;
    CsgOperation operation = CsgOperation::unite;
    std::vector<CsgNode> children;
};

struct Csg {
    Position where;
    std::string material;
    CsgNode root;
};

using Object = std::variant<Sphere, Torus, Mesh, Text, Csg, SceneMeshKind>;

struct Scene {
    Camera camera;
    Room room;
    std::vector<NamedMaterial> materials;
    Light light;
    std::vector<Object> objects;
    std::string text;
    std::string source;
};

}
