#pragma once

#include "geometry/analytic.h"
#include "components/geometry/quadric/model.h"
#include "components/geometry/box/model.h"
#include <type_traits>

#if defined(__HIPCC__)
#define GEOMETRY_HD __host__ __device__
#else
#define GEOMETRY_HD
#endif

enum class GeometryKind : std::uint8_t { quadric, torus, box };

struct GpuGeometry {
    GeometryKind kind = GeometryKind::quadric;
    union {
        hip_demo::GpuQuadric quadric;
        hip_demo::Torus torus;
        hip_demo::Box box;
    };
    GEOMETRY_HD GpuGeometry() : quadric{} {}
    GEOMETRY_HD GpuGeometry(hip_demo::GpuQuadric value) : kind(GeometryKind::quadric), quadric(value) {}
    GEOMETRY_HD GpuGeometry(hip_demo::Torus value) : kind(GeometryKind::torus), torus(value) {}
    GEOMETRY_HD GpuGeometry(hip_demo::Box value) : kind(GeometryKind::box), box(value) {}

    std::array<SceneVector, 2> bounds(float margin = 0) const {
        return kind == GeometryKind::quadric ? quadric.bounds(margin) :
               kind == GeometryKind::box ? box.bounds(margin) : torus.bounds(margin);
    }
};

static_assert(std::is_trivially_copyable_v<GpuGeometry>);

#undef GEOMETRY_HD
