#pragma once

#include "components/csg/description.h"
#include "components/csg/model.h"
#include "components/sphere/builder.h"
#include "components/torus/builder.h"
#include "components/text/builder.h"
#include <type_traits>

namespace hip_demo::csg {
inline Model build(const Description& source, const SceneValues& values) {
    const auto build = [&](auto&& self, const scene_ast::CsgNode& node) -> CsgNode {
        CsgNode result;
        result.operation = node.operation;
        if (node.children.empty()) {
            result.geometry = std::visit([&](const auto& shape) -> decltype(result.geometry) {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, scene_ast::Sphere>) return sphere::build(shape, values);
                else if constexpr (std::is_same_v<T, scene_ast::Torus>) return torus::build(shape, values);
                else {
                    static_assert(std::is_same_v<T, scene_ast::Text>);
                    return text::build(shape, values);
                }
            }, node.shape);
        } else {
            if (node.children.size() != 2)
                values.errors.fail(node.where, "CSG operations require exactly two operands");
            for (const auto& child : node.children) result.children.push_back(self(self, child));
        }
        return result;
    };
    return {build(build, source.root)};
}
}
