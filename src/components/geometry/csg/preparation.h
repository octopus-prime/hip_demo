#pragma once

#include "components/scene/prepared.h"
#include "components/geometry/builder.h"
#include "components/material/builder.h"
#include <algorithm>
#include <stdexcept>

namespace hip_demo {

inline void append_csg(PreparedScene& scene, const Csg& source, GpuMaterial material,
                       std::vector<ExtrudedText>& texts,
                       const std::vector<std::optional<Material>>& materials = {}) {
    GpuCsg object;
    object.first_leaf = static_cast<std::uint32_t>(scene.csg_leaves.size());
    object.material = material;
    std::size_t material_index = materials.size();
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
                result.shape = prepare_geometry(shape);
            }
            return result;
        }, node.geometry);
        leaf.when_inside = yes;
        leaf.when_outside = no;
        const auto index = static_cast<std::uint32_t>(scene.csg_leaves.size());
        // Bindings follow left-to-right leaves; Boolean compilation visits right first.
        GpuMaterial surface_material = material;
        if (!materials.empty()) {
            if (material_index == 0) throw std::runtime_error("CSG material bindings must match its leaves");
            const auto& binding = materials[--material_index];
            if (binding) surface_material = prepare_material(*binding);
        }
        scene.csg_materials.push_back(surface_material);
        scene.csg_leaves.push_back(leaf);
        return index;
    };
    object.entry = compile(compile, source.root, csg_inside, csg_outside);
    if (material_index != 0) throw std::runtime_error("CSG material bindings must match its leaves");
    object.leaf_count = static_cast<std::uint32_t>(scene.csg_leaves.size()) - object.first_leaf;
    scene.csgs.push_back(object);
}

}
