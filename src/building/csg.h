#pragma once

#include "building/scene_values.h"

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
        const auto primitive = std::visit([&](const auto& shape) {
            return SceneValues{errors}.primitive(shape);
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
