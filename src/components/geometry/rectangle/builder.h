#pragma once

#include "components/geometry/rectangle/description.h"
#include "components/geometry/rectangle/model.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::rectangle {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.positive(source.width, source.where),
            values.positive(source.depth, source.where)};
}
}
