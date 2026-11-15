#pragma once

#include "components/object/description.h"
#include "components/object/model.h"
#include "components/geometry/sphere/builder.h"
#include "components/geometry/torus/builder.h"
#include "components/geometry/text/builder.h"
#include "components/geometry/csg/builder.h"
#include "components/geometry/mesh/builder.h"
#include "components/geometry/rectangle/builder.h"
#include "components/geometry/ellipsoid/builder.h"
#include "components/geometry/cylinder/builder.h"
#include "components/geometry/cone/builder.h"
#include "components/geometry/paraboloid/builder.h"
#include "components/geometry/hyperboloid/builder.h"
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
            return {hip_demo::geometry::sphere::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Torus>)
            return {hip_demo::geometry::torus::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Text>)
            return {hip_demo::geometry::text::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Csg>)
            return {hip_demo::geometry::csg::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Mesh>)
            return {hip_demo::geometry::mesh::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Rectangle>)
            return {hip_demo::geometry::rectangle::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Ellipsoid>)
            return {hip_demo::geometry::ellipsoid::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Cylinder>)
            return {hip_demo::geometry::cylinder::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Cone>)
            return {hip_demo::geometry::cone::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Paraboloid>)
            return {hip_demo::geometry::paraboloid::build(value, values), material_reference(value)};
        else if constexpr (std::is_same_v<T, scene_ast::Hyperboloid>)
            return {hip_demo::geometry::hyperboloid::build(value, values), material_reference(value)};
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
