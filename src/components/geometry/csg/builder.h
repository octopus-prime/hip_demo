#pragma once

#include "components/geometry/csg/description.h"
#include "components/geometry/csg/model.h"
#include "components/geometry/sphere/builder.h"
#include "components/geometry/torus/builder.h"
#include "components/geometry/text/builder.h"
#include <type_traits>

namespace hip_demo::geometry::csg {
inline Model build(const Description& source, const SceneValues& values) {
    const auto build = [&](auto&& self, const scene_ast::CsgNode& node) -> CsgNode {
        CsgNode result;
        result.operation = node.operation;
        if (node.children.empty()) {
            result.geometry = std::visit([&](const auto& shape) -> decltype(result.geometry) {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, scene_ast::Sphere>) return hip_demo::geometry::sphere::build(shape, values);
                else if constexpr (std::is_same_v<T, scene_ast::Torus>) return hip_demo::geometry::torus::build(shape, values);
                else {
                    static_assert(std::is_same_v<T, scene_ast::Text>);
                    return hip_demo::geometry::text::build(shape, values);
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
