#pragma once

#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::rectangle {
struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double width, depth;
    std::string material;
};
}
namespace scene_ast { using Rectangle = hip_demo::geometry::rectangle::Description; }
