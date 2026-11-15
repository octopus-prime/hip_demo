#pragma once

#include "components/texture/color/description.h"
#include "components/texture/color/model.h"
#include "components/value/builder.h"

namespace hip_demo::texture::color {

inline Model build(const Description& source, scene_ast::Position where, const SceneValues& values) {
    return {values.color(source.color, where)};
}

}
