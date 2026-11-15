#pragma once

#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::box {
struct Description {
    scene_ast::Position where;
    scene_ast::Vector center, size;
    std::string material;
};
}
namespace scene_ast { using Box = hip_demo::geometry::box::Description; }
