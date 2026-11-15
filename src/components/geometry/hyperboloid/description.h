#pragma once
#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::hyperboloid {
struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double radius, axial_radius, height;
    std::string material;
};
}
namespace scene_ast { using Hyperboloid = hip_demo::geometry::hyperboloid::Description; }
