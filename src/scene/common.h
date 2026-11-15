#pragma once

#include <cstdint>

enum class MeshUp { y, z };
enum class ShaderKind : std::uint8_t { diffuse, metal, glass, phong, emissive };
enum class TextureKind : std::uint8_t { color, tiles, wood, mandelbrot, julia, marble };
enum class SceneMeshKind { ply, cat, crystal };
enum class CsgOperation { unite, intersection, difference };

struct SceneVector {
    float x = 0, y = 0, z = 0;
    bool operator==(const SceneVector&) const = default;
};

struct SceneLight {
    SceneVector center;
    SceneVector radiance;
    float width = 0, depth = 0;
    float major_radius = 0, minor_radius = 0;
    bool torus = false;
};

inline constexpr std::uint32_t csg_inside = ~std::uint32_t{0};
inline constexpr std::uint32_t csg_outside = csg_inside - 1;
