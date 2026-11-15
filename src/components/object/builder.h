#pragma once

#include "components/object/description.h"
#include "components/object/model.h"
#include "components/sphere/builder.h"
#include "components/torus/builder.h"
#include "components/text/builder.h"
#include "components/csg/builder.h"
#include "components/mesh/builder.h"
#include <map>
#include <type_traits>
#include <utility>

namespace hip_demo::object {
inline Model build(const Description& source, const std::map<std::string, Material>& materials,
                   const SceneValues& values) {
    const auto& errors = values.errors;
    const auto material_reference = [&](const auto& object) {
        const auto found = materials.find(object.material);
        if (found == materials.end()) errors.fail(object.where, "unknown material '" + object.material + "'");
        return found->second;
    };
    return std::visit([&](const auto& value) -> Model {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, scene_ast::Sphere>)
            return {sphere::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Torus>)
            return {torus::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Text>)
            return {text::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Csg>)
            return {csg::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Mesh>)
            return {mesh::build(value, values), material_reference(value)};
        else {
            static_assert(std::is_same_v<T, SceneMeshKind>);
            Mesh geometry;
            geometry.kind = value;
            Material material = value == SceneMeshKind::crystal
                ? Material{Phong{ConstantColor{{0.10f, 0.65f, 0.72f}}}}
                : Material{Diffuse{ConstantColor{{0.78f, 0.36f, 0.16f}}}};
            return {std::move(geometry), std::move(material)};
        }
    }, source);
}
}
