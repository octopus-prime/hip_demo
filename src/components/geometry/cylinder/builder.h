#pragma once
#include "components/geometry/cylinder/description.h"
#include "components/geometry/cylinder/model.h"
#include "components/geometry/quadric/builder.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::cylinder {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.positive(source.radius, source.where),
            values.positive(source.height, source.where)};
}
inline GpuQuadric prepare(const Model& source) {
    GpuQuadric result;
    result.center = source.center;
    result.scale = {source.radius, source.height / 2, source.radius};
    result.diagonal = {1, 0, 1};
    result.capped = true;
    return quadric::prepare(result);
}
}
