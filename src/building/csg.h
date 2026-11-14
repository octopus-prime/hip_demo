#pragma once

#include "scene/ast.h"
#include "parsing/diagnostics.h"

#include <cmath>
#include <type_traits>

namespace hip_demo {

inline void append_csg(SceneDescription& scene, const scene_ast::Csg& source,
                       SceneMaterial material, const SceneDiagnostics& errors) {
    SceneCsg object;
    object.first_leaf = static_cast<std::uint32_t>(scene.csg_leaves.size());
    object.material = material;
    // Compile short-circuit Boolean evaluation into leaf-to-leaf jumps.
    // The GPU needs neither recursion nor a fixed-size expression stack.
    const auto compile = [&](auto&& self, const scene_ast::CsgNode& node,
                              std::uint32_t yes, std::uint32_t no) -> std::uint32_t {
        if (!node.children.empty()) {
            if (node.children.size() != 2) errors.fail(node.where, "CSG operations require exactly two operands");
            const bool subtract = node.operation == CsgOperation::difference;
            const auto right = self(self, node.children[1], subtract ? no : yes, subtract ? yes : no);
            return node.operation == CsgOperation::unite
                ? self(self, node.children[0], yes, right)
                : self(self, node.children[0], right, no);
        }
        if (scene.csg_leaves.size() >= csg_outside)
            errors.fail(node.where, "CSG exceeds the leaf index limit");
        ScenePrimitive primitive;
        std::visit([&](const auto& shape) {
            primitive.center = shape.center;
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, scene_ast::Sphere>) {
                primitive.radius = shape.radius;
            } else {
                if (shape.minor_radius >= shape.major_radius)
                    errors.fail(shape.where, "minor_radius must be less than major_radius");
                primitive.sphere = false;
                primitive.major_radius = shape.major_radius;
                primitive.minor_radius = shape.minor_radius;
                const double angle = static_cast<double>(shape.tilt_x) * 3.141592653589793 / 180;
                primitive.tilt_sine = static_cast<float>(std::sin(angle));
                primitive.tilt_cosine = static_cast<float>(std::cos(angle));
            }
        }, node.shape);
        const auto index = static_cast<std::uint32_t>(scene.csg_leaves.size());
        scene.csg_leaves.push_back({primitive, yes, no});
        return index;
    };
    object.entry = compile(compile, source.root, csg_inside, csg_outside);
    object.leaf_count = static_cast<std::uint32_t>(scene.csg_leaves.size()) - object.first_leaf;
    scene.csgs.push_back(object);
}

}
