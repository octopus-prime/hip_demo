#pragma once

#include "components/light/description.h"
#include "components/light/model.h"
#include "components/rectangle_light/builder.h"
#include "components/torus_light/builder.h"
#include <type_traits>

namespace hip_demo::light {
inline Model build(const Description& source, const room::Model& room, const SceneValues& values) {
    return std::visit([&](const auto& light) -> Model {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, rectangle_light::Description>)
            return rectangle_light::build(light, room, values);
        else
            return torus_light::build(light, values);
    }, source);
}

inline SceneLight prepare(const Model& value) {
    return std::visit([](const auto& light) {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, rectangle_light::Model>)
            return rectangle_light::prepare(light);
        else
            return torus_light::prepare(light);
    }, value);
}
}
