#pragma once

#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::torus {

struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double major_radius, minor_radius, tilt_x;
    std::string material;
};
}
namespace scene_ast { using Torus = hip_demo::geometry::torus::Description; }
