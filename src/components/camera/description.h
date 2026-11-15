#pragma once

#include "components/value/description.h"

namespace hip_demo::camera {

struct Description {
    scene_ast::Position where;
    scene_ast::Vector position, target;
    double half_view_height;
};
}
namespace scene_ast { using Camera = hip_demo::camera::Description; }
