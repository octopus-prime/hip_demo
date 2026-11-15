#pragma once

#include "components/geometry/text/description.h"
#include "components/geometry/text/model.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::text {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.string(source.font, source.where), values.string(source.value, source.where),
            values.positive(source.size, source.where), values.positive(source.depth, source.where),
            values.vector(source.position, source.where)};
}
}
