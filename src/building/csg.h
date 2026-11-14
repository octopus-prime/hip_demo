#pragma once

#include "building/scene_values.h"

#include <algorithm>
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
        auto leaf = std::visit([&](const auto& shape) {
            SceneCsgLeaf result;
            if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, scene_ast::Text>) {
                const auto text = SceneValues{errors}.text(shape);
                const auto same = std::find_if(scene.csg_texts.begin(), scene.csg_texts.end(),
                    [&](const SceneText& other) {
                        return text.font == other.font && text.value == other.value &&
                            text.size == other.size && text.depth == other.depth &&
                            text.position.x == other.position.x && text.position.y == other.position.y &&
                            text.position.z == other.position.z;
                    });
                result.text = true;
                result.text_index = static_cast<std::uint32_t>(same - scene.csg_texts.begin());
                if (same == scene.csg_texts.end()) scene.csg_texts.push_back(text);
            } else {
                result.shape = SceneValues{errors}.primitive(shape);
            }
            return result;
        }, node.shape);
        leaf.when_inside = yes;
        leaf.when_outside = no;
        const auto index = static_cast<std::uint32_t>(scene.csg_leaves.size());
        scene.csg_leaves.push_back(leaf);
        return index;
    };
    object.entry = compile(compile, source.root, csg_inside, csg_outside);
    object.leaf_count = static_cast<std::uint32_t>(scene.csg_leaves.size()) - object.first_leaf;
    scene.csgs.push_back(object);
}

}
