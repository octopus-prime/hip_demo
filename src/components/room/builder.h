#pragma once

#include "components/room/description.h"
#include "components/room/model.h"
#include "components/texture/builder.h"

namespace hip_demo::room {
inline Model build(const Description& source, const SceneValues& values) {
    return {values.dimensions(source.size, source.where), {
        texture::build(source.floor, source.where, values),
        texture::build(source.ceiling, source.where, values),
        texture::build(source.back, source.where, values),
        texture::build(source.left, source.where, values),
        texture::build(source.right, source.where, values)}};
}
}
