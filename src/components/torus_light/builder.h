#pragma once

#include "components/torus_light/description.h"
#include "components/torus_light/model.h"
#include "components/light/gpu_model.h"
#include "components/value/builder.h"

namespace hip_demo::torus_light {
inline Model build(const Description& source, const SceneValues& values) {
    const auto radiance = values.radiance(source.radiance, source.where);
    const auto sizes = values.radii(source.major_radius, source.minor_radius, source.where);
    return {values.vector(source.center, source.where), radiance, sizes[0], sizes[1]};
}

inline SceneLight prepare(const Model& value) {
    return {value.center, value.radiance, 0, 0, value.major_radius, value.minor_radius, true};
}
}
