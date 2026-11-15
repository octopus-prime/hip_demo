#pragma once
#include "components/geometry/hyperboloid/description.h"
#include "components/geometry/hyperboloid/model.h"
#include "components/geometry/quadric/builder.h"
#include "components/value/builder.h"

namespace hip_demo::geometry::hyperboloid {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.vector(source.center, source.where), values.positive(source.radius, source.where),
            values.positive(source.axial_radius, source.where), values.positive(source.height, source.where)};
}
inline GpuQuadric prepare(const Model& source) {
    if (!std::isfinite(source.radius) || !std::isfinite(source.axial_radius) || !std::isfinite(source.height) ||
        source.radius <= 0 || source.axial_radius <= 0 || source.height <= 0)
        throw std::runtime_error("Hyperboloid dimensions must be finite and positive");
    const double ratio = (double(source.height) / 2) / source.axial_radius;
    const double denominator = 1 + ratio * ratio;
    const float radial_bound = static_cast<float>(source.radius * std::sqrt(denominator));
    const float waist_squared = static_cast<float>(1 / denominator);
    if (!(waist_squared > 0))
        throw std::runtime_error("Hyperboloid waist exceeds the usable float range");
    GpuQuadric result;
    result.center = source.center;
    result.scale = {radial_bound, source.height / 2, radial_bound};
    result.diagonal = {1, -static_cast<float>(ratio * ratio / denominator), 1};
    result.constant = -waist_squared;
    result.capped = true;
    return quadric::prepare(result);
}
}
