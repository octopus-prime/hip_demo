#pragma once

#include "components/value/description.h"
#include <string>

namespace hip_demo::sphere {

struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double radius;
    std::string material;
};
}
namespace scene_ast { using Sphere = hip_demo::sphere::Description; }
