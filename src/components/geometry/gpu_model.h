#pragma once

#include "geometry/analytic.h"
#include <type_traits>

#if defined(__HIPCC__)
#define GEOMETRY_HD __host__ __device__
#else
#define GEOMETRY_HD
#endif

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

static_assert(std::is_trivially_copyable_v<GpuGeometry>);

#undef GEOMETRY_HD
