#pragma once

#include "components/geometry/quadric/model.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hip_demo::geometry::quadric {
inline Model prepare(Model value) {
    if (value.axis != Axis::x && value.axis != Axis::y && value.axis != Axis::z)
        throw std::runtime_error("Invalid quadric cap axis");
    const auto finite = [](SceneVector v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(value.center) || !finite(value.scale) || !finite(value.diagonal) || !finite(value.cross) ||
        !finite(value.linear) || !std::isfinite(value.constant))
        throw std::runtime_error("Quadric parameters must be finite");
    const auto box = value.bounds();
    const std::array<float, 3> scales{value.scale.x, value.scale.y, value.scale.z};
    const std::array<float, 3> low{box[0].x, box[0].y, box[0].z}, high{box[1].x, box[1].y, box[1].z};
    const std::array<double, 3> gradients{
        2 * std::abs(double(value.diagonal.x)) + std::abs(double(value.cross.x)) +
            std::abs(double(value.cross.y)) + std::abs(double(value.linear.x)),
        2 * std::abs(double(value.diagonal.y)) + std::abs(double(value.cross.x)) +
            std::abs(double(value.cross.z)) + std::abs(double(value.linear.y)),
        2 * std::abs(double(value.diagonal.z)) + std::abs(double(value.cross.y)) +
            std::abs(double(value.cross.z)) + std::abs(double(value.linear.z))};
    for (std::size_t i = 0; i < 3; ++i) {
        if (scales[i] <= 0 || !std::isfinite(1.0f / scales[i]) ||
            gradients[i] / scales[i] > std::numeric_limits<float>::max())
            throw std::runtime_error("Quadric dimensions exceed the usable float range");
        if (!std::isfinite(low[i]) || !std::isfinite(high[i]) || low[i] >= high[i])
            throw std::runtime_error("Quadric bounds must be finite and nondegenerate");
    }
    return value;
}
}
