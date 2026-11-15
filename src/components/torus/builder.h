#pragma once

#include "components/torus/description.h"
#include "components/torus/model.h"
#include "components/value/builder.h"

namespace hip_demo::torus {
inline Model build(const Description& source, const SceneValues& values) {
    Torus result;
    result.center = values.vector(source.center, source.where);
    const auto sizes = values.radii(source.major_radius, source.minor_radius, source.where);
    result.major_radius = sizes[0];
    result.minor_radius = sizes[1];
    const double angle = static_cast<double>(values.scalar(source.tilt_x, source.where)) * 3.141592653589793 / 180;
    result.tilt_sine = static_cast<float>(std::sin(angle));
    result.tilt_cosine = static_cast<float>(std::cos(angle));
    return result;
}
}
