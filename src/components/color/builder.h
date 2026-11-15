#pragma once

#include "components/color/description.h"
#include "components/color/model.h"
#include "components/value/builder.h"

namespace hip_demo::color {

inline Model build(const Description& source, scene_ast::Position where, const SceneValues& values) {
    return {values.color(source.color, where)};
}

}
