#pragma once

#include "geometry/analytic.h"
#include <type_traits>

#if defined(__HIPCC__)
#define GEOMETRY_HD __host__ __device__
#else
#define GEOMETRY_HD
#endif

struct GpuTexture {
    SceneVector color;
    TextureKind kind = TextureKind::color;
};

struct GpuMaterial {
    SceneVector albedo;
    ShaderKind kind = ShaderKind::diffuse;
    float roughness = 1;
    TextureKind texture = TextureKind::color;
};

struct GpuSceneSettings {
    SceneVector camera_position;
    SceneVector camera_target;
    float half_view_height = 0;
    SceneVector room_size;
    GpuTexture textures[5];
    SceneLight light;
};

enum class GeometryKind : std::uint8_t { sphere, torus };

struct GpuGeometry {
    GeometryKind kind = GeometryKind::sphere;
    union {
        hip_demo::Sphere sphere;
        hip_demo::Torus torus;
    };
    GEOMETRY_HD GpuGeometry() : sphere{} {}
    GEOMETRY_HD GpuGeometry(hip_demo::Sphere value) : kind(GeometryKind::sphere), sphere(value) {}
    GEOMETRY_HD GpuGeometry(hip_demo::Torus value) : kind(GeometryKind::torus), torus(value) {}

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        return kind == GeometryKind::sphere ? sphere.bounds(margin) : torus.bounds(margin);
    }
};

struct GpuPrimitive {
    GpuGeometry geometry;
    GpuMaterial material;
};

struct GpuCsgLeaf {
    GpuGeometry shape;
    std::uint32_t when_inside = csg_inside, when_outside = csg_outside;
    std::uint32_t text_index = 0, first_glyph = 0, glyph_count = 0;
    bool text = false;
};

struct GpuCsg {
    std::uint32_t first_leaf = 0, leaf_count = 0, entry = 0;
    GpuMaterial material;
};

static_assert(std::is_trivially_copyable_v<GpuGeometry>);
static_assert(std::is_trivially_copyable_v<GpuPrimitive>);
static_assert(std::is_trivially_copyable_v<GpuCsgLeaf>);
static_assert(std::is_trivially_copyable_v<GpuMaterial>);

#undef GEOMETRY_HD
