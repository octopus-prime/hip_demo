#pragma once

#include "components/light/description.h"
#include "components/light/rectangle/builder.h"
#include "components/light/torus/builder.h"
#include <type_traits>

namespace hip_demo::light {
inline Object build(const Description& source, const room::Model& room, const SceneValues& values) {
    return std::visit([&](const auto& light) -> Object {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, hip_demo::light::rectangle::Description>)
            return hip_demo::light::rectangle::build(light, room, values);
        else
            return hip_demo::light::torus::build(light, values);
    }, source);
}

}
