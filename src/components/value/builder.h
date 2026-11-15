#pragma once

#include "components/value/description.h"
#include "scene/common.h"
#include "parsing/diagnostics.h"

#include <cmath>
#include <limits>

namespace hip_demo {

struct SceneValues {
    const SceneDiagnostics& errors;

    float scalar(double value, scene_ast::Position where) const {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            errors.fail(where, "number is outside the finite float range");
        return static_cast<float>(value);
    }

    float positive(double value, scene_ast::Position where) const {
        const float result = scalar(value, where);
        if (result <= 0) errors.fail(where, "value must be positive");
        return result;
    }

    SceneVector vector(scene_ast::Vector value, scene_ast::Position where) const {
        return {scalar(value.x, where), scalar(value.y, where), scalar(value.z, where)};
    }

    SceneVector dimensions(scene_ast::Vector value, scene_ast::Position where) const {
        const auto result = vector(value, where);
        if (result.x <= 0 || result.y <= 0 || result.z <= 0)
            errors.fail(where, "all dimensions must be positive");
        return result;
    }

    SceneVector color(scene_ast::Vector value, scene_ast::Position where) const {
        const auto result = vector(value, where);
        if (result.x < 0 || result.x > 1 || result.y < 0 || result.y > 1 || result.z < 0 || result.z > 1)
            errors.fail(where, "color components must be in [0, 1]");
        return result;
    }

    SceneVector radiance(scene_ast::Vector value, scene_ast::Position where) const {
        const auto result = vector(value, where);
        if (result.x < 0 || result.y < 0 || result.z < 0)
            errors.fail(where, "radiance must be nonnegative");
        return result;
    }

    const std::string& string(const std::string& value, scene_ast::Position where) const {
        if (value.empty() || value.find('\0') != std::string::npos)
            errors.fail(where, "strings must not be empty or contain NUL");
        return value;
    }

    std::array<float, 2> radii(double major, double minor, scene_ast::Position where) const {
        const float R = positive(major, where), r = positive(minor, where);
        if (r >= R) errors.fail(where, "minor_radius must be less than major_radius");
        return {R, r};
    }
};

}
