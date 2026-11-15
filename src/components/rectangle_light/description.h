#pragma once

#include "components/value/description.h"

namespace hip_demo::rectangle_light {

struct Description {
    scene_ast::Position where;
    std::array<double, 2> center, size;
    scene_ast::Vector radiance;
};
}
namespace scene_ast { using RectangleLight = hip_demo::rectangle_light::Description; }
