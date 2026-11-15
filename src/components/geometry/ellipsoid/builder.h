#pragma once
#include "components/geometry/ellipsoid/description.h"
#include "components/geometry/ellipsoid/model.h"
#include "components/geometry/quadric/builder.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::ellipsoid {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.dimensions(source.radii, source.where)};
}
inline GpuQuadric prepare(const Model& source) {
    GpuQuadric result;
    result.center = source.center;
    result.scale = source.radii;
    return quadric::prepare(result);
}
}
