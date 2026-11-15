#pragma once
#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::cylinder {
struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double radius, height;
    std::string material;
};
}
namespace scene_ast { using Cylinder = hip_demo::geometry::cylinder::Description; }
