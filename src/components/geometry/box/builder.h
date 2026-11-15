#pragma once

#include "components/geometry/box/description.h"
#include "components/geometry/box/model.h"
#include "components/value/builder.h"
#include <cmath>
#include <stdexcept>

namespace hip_demo::geometry::box {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.dimensions(source.size, source.where)};
}

inline Model prepare(const Model& value) {
    const auto bounds = value.bounds();
    const std::array<float, 3> sizes{value.size.x, value.size.y, value.size.z};
    const std::array<float, 3> low{bounds[0].x, bounds[0].y, bounds[0].z};
    const std::array<float, 3> high{bounds[1].x, bounds[1].y, bounds[1].z};
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(sizes[i]) || sizes[i] <= 0 || !std::isfinite(low[i]) ||
            !std::isfinite(high[i]) || low[i] >= high[i])
            throw std::runtime_error("Box bounds must be finite and nondegenerate");
    return value;
}
}
