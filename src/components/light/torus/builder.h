#pragma once

#include "components/light/torus/description.h"
#include "components/geometry/torus/model.h"
#include "components/object/model.h"
#include "components/value/builder.h"

namespace hip_demo::light::torus {
inline Object build(const Description& source, const SceneValues& values) {
    const auto radiance = values.radiance(source.radiance, source.where);
    const auto sizes = values.radii(source.major_radius, source.minor_radius, source.where);
    return {Torus{values.vector(source.center, source.where), sizes[0], sizes[1]},
            Emissive{ConstantColor{radiance}}};
}
}
