#pragma once

#include "components/scene/prepared.h"
#include <algorithm>
#include <stdexcept>

namespace hip_demo {

inline void append_csg(PreparedScene& scene, const Csg& source, GpuMaterial material,
                       std::vector<ExtrudedText>& texts) {
    GpuCsg object;
    object.first_leaf = static_cast<std::uint32_t>(scene.csg_leaves.size());
    object.material = material;
    // Compile short-circuit Boolean evaluation into leaf-to-leaf jumps.
    // The GPU needs neither recursion nor a fixed-size expression stack.
    const auto compile = [&](auto&& self, const CsgNode& node,
                              std::uint32_t yes, std::uint32_t no) -> std::uint32_t {
        if (!node.children.empty()) {
            if (node.children.size() != 2) throw std::runtime_error("CSG operations require exactly two operands");
            const bool subtract = node.operation == CsgOperation::difference;
            const auto right = self(self, node.children[1], subtract ? no : yes, subtract ? yes : no);
            return node.operation == CsgOperation::unite
                ? self(self, node.children[0], yes, right)
                : self(self, node.children[0], right, no);
        }
        if (scene.csg_leaves.size() >= csg_outside)
            throw std::runtime_error("CSG exceeds the leaf index limit");
        auto leaf = std::visit([&](const auto& shape) {
            GpuCsgLeaf result;
            if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, ExtrudedText>) {
                const auto same = std::find(texts.begin(), texts.end(), shape);
                result.text = true;
                result.text_index = static_cast<std::uint32_t>(same - texts.begin());
                if (same == texts.end()) texts.push_back(shape);
            } else {
                result.shape = GpuGeometry{shape};
            }
            return result;
        }, node.geometry);
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
