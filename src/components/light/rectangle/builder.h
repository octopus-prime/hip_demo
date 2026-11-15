#pragma once

#include "components/light/rectangle/description.h"
#include "components/light/rectangle/model.h"
#include "components/room/model.h"
#include "components/light/gpu_model.h"
#include "components/value/builder.h"

namespace hip_demo::light::rectangle {
inline Model build(const Description& source, const room::Model& room, const SceneValues& values) {
    Model result;
    result.radiance = values.radiance(source.radiance, source.where);
    result.center = {values.scalar(source.center[0], source.where), room.size.y,
                     values.scalar(source.center[1], source.where)};
    result.width = values.positive(source.size[0], source.where);
    result.depth = values.positive(source.size[1], source.where);
    if (std::abs(result.center.x) + result.width / 2 > room.size.x / 2 ||
        std::abs(result.center.z) + result.depth / 2 > room.size.z / 2) {
        values.errors.fail(source.where, "ceiling light must fit inside the room");
    }
    return result;
}

inline SceneLight prepare(const Model& value) {
    return {value.center, value.radiance, value.width, value.depth};
}
}
