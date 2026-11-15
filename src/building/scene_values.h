#pragma once

#include "scene/ast.h"
#include "scene/types.h"
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

    Material material(const scene_ast::MaterialValue& value, scene_ast::Position where) const {
        const auto albedo = color(value.albedo, where);
        const float roughness = scalar(value.roughness, where);
        if (roughness < 0 || roughness > 1) errors.fail(where, "roughness must be in [0, 1]");
        const Texture texture = ConstantColor{albedo};
        switch (value.kind) {
        case ShaderKind::diffuse: return Diffuse{texture};
        case ShaderKind::phong: return Phong{texture};
        case ShaderKind::metal: return Metal{texture, roughness};
        case ShaderKind::glass: return Glass{texture};
        case ShaderKind::emissive: return Emissive{texture};
        }
        errors.fail(where, "unknown shader kind");
    }

    Texture texture(const scene_ast::TextureValue& value, scene_ast::Position where) const {
        const auto rgb = color(value.color, where);
        switch (value.kind) {
        case TextureKind::color: return ConstantColor{rgb};
        case TextureKind::tiles: return Tiles{};
        case TextureKind::wood: return Wood{};
        case TextureKind::marble: return Marble{};
        case TextureKind::mandelbrot: return Mandelbrot{};
        case TextureKind::julia: return Julia{};
        }
        errors.fail(where, "unknown texture kind");
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

    Sphere geometry(const scene_ast::Sphere& source) const {
        return {vector(source.center, source.where), positive(source.radius, source.where)};
    }

    ExtrudedText geometry(const scene_ast::Text& source) const {
        return {string(source.font, source.where), string(source.value, source.where),
                positive(source.size, source.where), positive(source.depth, source.where),
                vector(source.position, source.where)};
    }

    Torus geometry(const scene_ast::Torus& source) const {
        Torus result;
        result.center = vector(source.center, source.where);
        const auto sizes = radii(source.major_radius, source.minor_radius, source.where);
        result.major_radius = sizes[0];
        result.minor_radius = sizes[1];
        const double angle = static_cast<double>(scalar(source.tilt_x, source.where)) * 3.141592653589793 / 180;
        result.tilt_sine = static_cast<float>(std::sin(angle));
        result.tilt_cosine = static_cast<float>(std::cos(angle));
        return result;
    }

    Csg geometry(const scene_ast::Csg& source) const {
        const auto build = [&](auto&& self, const scene_ast::CsgNode& node) -> CsgNode {
            CsgNode result;
            result.operation = node.operation;
            if (node.children.empty()) {
                result.geometry = std::visit([&](const auto& shape) -> decltype(result.geometry) {
                    return geometry(shape);
                }, node.shape);
            } else {
                if (node.children.size() != 2)
                    errors.fail(node.where, "CSG operations require exactly two operands");
                for (const auto& child : node.children) result.children.push_back(self(self, child));
            }
            return result;
        };
        return {build(build, source.root)};
    }
};

}
