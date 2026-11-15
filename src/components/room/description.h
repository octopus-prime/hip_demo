#pragma once

#include "components/value/description.h"
#include "components/texture/description.h"

namespace hip_demo::room {

struct Description {
    scene_ast::Position where;
    scene_ast::Vector size;
    scene_ast::TextureValue floor, ceiling, back, left, right;
};
}
namespace scene_ast { using Room = hip_demo::room::Description; }
