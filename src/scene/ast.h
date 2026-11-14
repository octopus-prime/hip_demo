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

struct Camera {
    Position where;
    SceneVector position, target;
    float half_view_height;
};

struct Room {
    SceneVector size;
    SceneSurface floor, ceiling, back, left, right;
};

struct NamedMaterial {
    Position where;
    std::string name;
    SceneMaterial value;
};

struct RectangleLight {
    Position where;
    std::array<float, 2> center, size;
    SceneVector radiance;
};

struct TorusLight {
    Position where;
    SceneVector center;
    float major_radius, minor_radius;
    SceneVector radiance;
};

struct Sphere {
    Position where;
    SceneVector center;
    float radius;
    std::string material;
};

struct Torus {
    Position where;
    SceneVector center;
    float major_radius, minor_radius, tilt_x;
    std::string material;
};

struct Mesh {
    Position where;
    std::string file;
    MeshUp up;
    SceneVector fit, position;
    std::string material;
};

struct Text {
    Position where;
    std::string font, value;
    float size, depth;
    SceneVector position;
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
