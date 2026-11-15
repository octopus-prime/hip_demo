#pragma once

#include "components/geometry/sphere/description.h"
#include "components/geometry/sphere/model.h"
#include "components/value/builder.h"
#include "components/geometry/quadric/builder.h"

namespace hip_demo::geometry::sphere {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.positive(source.radius, source.where)};
}
inline GpuQuadric prepare(const Model& source) {
    GpuQuadric result;
    result.center = source.center;
    result.scale = {source.radius, source.radius, source.radius};
    return quadric::prepare(result);
}
}
