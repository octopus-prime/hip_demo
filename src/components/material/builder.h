#pragma once

#include "components/material/description.h"
#include "components/material/model.h"
#include "components/diffuse/builder.h"
#include "components/phong/builder.h"
#include "components/metal/builder.h"
#include "components/glass/builder.h"
#include "components/emissive/builder.h"

namespace hip_demo::material {
inline Model build(const scene_ast::MaterialValue& value, scene_ast::Position where, const SceneValues& values) {
    return std::visit([&](const auto& shader) -> Model {
        using T = std::decay_t<decltype(shader)>;
        if constexpr (std::is_same_v<T, diffuse::Description>) return diffuse::build(shader, where, values);
        else if constexpr (std::is_same_v<T, phong::Description>) return phong::build(shader, where, values);
        else if constexpr (std::is_same_v<T, metal::Description>) return metal::build(shader, where, values);
        else if constexpr (std::is_same_v<T, glass::Description>) return glass::build(shader, where, values);
        else {
            static_assert(std::is_same_v<T, emissive::Description>);
            return emissive::build(shader, where, values);
        }
    }, value);
}
}
namespace hip_demo {
inline GpuMaterial prepare_material(const Material& material) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, diffuse::Model>) return diffuse::prepare(value);
        else if constexpr (std::is_same_v<T, phong::Model>) return phong::prepare(value);
        else if constexpr (std::is_same_v<T, metal::Model>) return metal::prepare(value);
        else if constexpr (std::is_same_v<T, glass::Model>) return glass::prepare(value);
        else {
            static_assert(std::is_same_v<T, emissive::Model>);
            return emissive::prepare(value);
        }
    }, material);
}
}
