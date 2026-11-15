#pragma once
#include "components/geometry/cylinder/description.h"
#include "components/geometry/cylinder/model.h"
#include "components/geometry/quadric/builder.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::cylinder {
inline Model build(const Description& source, const SceneValues& values) {
    if (source.axis != Axis::x && source.axis != Axis::y && source.axis != Axis::z)
        values.errors.fail(source.where, "invalid cylinder axis");
    return {values.vector(source.center, source.where), values.positive(source.radius, source.where),
            values.positive(source.height, source.where), source.axis};
}
inline GpuQuadric prepare(const Model& source) {
    GpuQuadric result;
    result.center = source.center;
    result.scale = {source.radius, source.height / 2, source.radius};
    result.diagonal = {1, 0, 1};
    result.axis = source.axis;
    if (source.axis == Axis::x) {
        result.scale = {source.height / 2, source.radius, source.radius};
        result.diagonal = {0, 1, 1};
    } else if (source.axis == Axis::z) {
        result.scale = {source.radius, source.radius, source.height / 2};
        result.diagonal = {1, 1, 0};
    }
    result.capped = true;
    return quadric::prepare(result);
}
}
