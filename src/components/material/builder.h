#pragma once

#include "components/material/description.h"
#include "components/material/model.h"
#include "components/material/diffuse/builder.h"
#include "components/material/phong/builder.h"
#include "components/material/metal/builder.h"
#include "components/material/glass/builder.h"
#include "components/material/emissive/builder.h"

namespace hip_demo::material {
inline Model build(const scene_ast::MaterialValue& value, scene_ast::Position where, const SceneValues& values) {
    return std::visit([&](const auto& shader) -> Model {
        using T = std::decay_t<decltype(shader)>;
        if constexpr (std::is_same_v<T, hip_demo::material::diffuse::Description>) return hip_demo::material::diffuse::build(shader, where, values);
        else if constexpr (std::is_same_v<T, hip_demo::material::phong::Description>) return hip_demo::material::phong::build(shader, where, values);
        else if constexpr (std::is_same_v<T, hip_demo::material::metal::Description>) return hip_demo::material::metal::build(shader, where, values);
        else if constexpr (std::is_same_v<T, hip_demo::material::glass::Description>) return hip_demo::material::glass::build(shader, where, values);
        else {
            static_assert(std::is_same_v<T, hip_demo::material::emissive::Description>);
            return hip_demo::material::emissive::build(shader, where, values);
        }
    }, value);
}
}
namespace hip_demo {
inline GpuMaterial prepare_material(const Material& material) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, hip_demo::material::diffuse::Model>) return hip_demo::material::diffuse::prepare(value);
        else if constexpr (std::is_same_v<T, hip_demo::material::phong::Model>) return hip_demo::material::phong::prepare(value);
        else if constexpr (std::is_same_v<T, hip_demo::material::metal::Model>) return hip_demo::material::metal::prepare(value);
        else if constexpr (std::is_same_v<T, hip_demo::material::glass::Model>) return hip_demo::material::glass::prepare(value);
        else {
            static_assert(std::is_same_v<T, hip_demo::material::emissive::Model>);
            return hip_demo::material::emissive::prepare(value);
        }
    }, material);
}
}
