#pragma once

#include "components/camera/description.h"
#include "components/camera/model.h"
#include "components/value/builder.h"

namespace hip_demo::camera {
inline Model build(const Description& source, const SceneValues& values) {
    Model result;
    result.position = values.vector(source.position, source.where);
    result.target = values.vector(source.target, source.where);
    result.half_view_height = values.positive(source.half_view_height, source.where);
    const double dx = static_cast<double>(result.position.x) - result.target.x;
    const double dz = static_cast<double>(result.position.z) - result.target.z;
    if (dx * dx + dz * dz < 1e-12) {
        values.errors.fail(source.where, "camera direction must not be zero or parallel to the Y axis");
    }
    return result;
}
}
