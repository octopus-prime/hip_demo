#pragma once

#include "scene/ast.h"
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

    SceneMaterial material(const scene_ast::MaterialValue& value, scene_ast::Position where) const {
        const auto albedo = color(value.albedo, where);
        const float roughness = scalar(value.roughness, where);
        if (roughness < 0 || roughness > 1) errors.fail(where, "roughness must be in [0, 1]");
        return {albedo, value.kind, roughness};
    }

    SceneSurface surface(const scene_ast::Surface& value, scene_ast::Position where) const {
        return {color(value.color, where), value.kind};
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

    ScenePrimitive primitive(const scene_ast::Sphere& source) const {
        ScenePrimitive result;
        result.center = vector(source.center, source.where);
        result.radius = positive(source.radius, source.where);
        return result;
    }

    ScenePrimitive primitive(const scene_ast::Torus& source) const {
        ScenePrimitive result;
        result.center = vector(source.center, source.where);
        const auto sizes = radii(source.major_radius, source.minor_radius, source.where);
        result.sphere = false;
        result.major_radius = sizes[0];
        result.minor_radius = sizes[1];
        const double angle = static_cast<double>(scalar(source.tilt_x, source.where)) * 3.141592653589793 / 180;
        result.tilt_sine = static_cast<float>(std::sin(angle));
        result.tilt_cosine = static_cast<float>(std::cos(angle));
        return result;
    }
};

}
