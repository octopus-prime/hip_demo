#pragma once

#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::text {

struct Description {
    scene_ast::Position where;
    std::string font, value;
    double size, depth;
    scene_ast::Vector position;
    std::string material;
};
}
namespace scene_ast { using Text = hip_demo::geometry::text::Description; }
