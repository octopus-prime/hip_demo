#pragma once

#include "components/light/description.h"
#include "components/light/model.h"
#include "components/light/rectangle/builder.h"
#include "components/light/torus/builder.h"
#include <type_traits>

namespace hip_demo::light {
inline Model build(const Description& source, const room::Model& room, const SceneValues& values) {
    return std::visit([&](const auto& light) -> Model {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, hip_demo::light::rectangle::Description>)
            return hip_demo::light::rectangle::build(light, room, values);
        else
            return hip_demo::light::torus::build(light, values);
    }, source);
}

inline SceneLight prepare(const Model& value) {
    return std::visit([](const auto& light) {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, hip_demo::light::rectangle::Model>)
            return hip_demo::light::rectangle::prepare(light);
        else
            return hip_demo::light::torus::prepare(light);
    }, value);
}
}
