#pragma once

#include "components/geometry/mesh/description.h"
#include "components/geometry/mesh/model.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::mesh {
inline Model build(const Description& source, const SceneValues& values) {
    Model result;
    result.file = values.string(source.file, source.where);
    result.up = source.up;
    result.fit = values.dimensions(source.fit, source.where);
    result.position = values.vector(source.position, source.where);
    return result;
}
}
