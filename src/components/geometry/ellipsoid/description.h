#pragma once
#include "components/value/description.h"
#include <string>

namespace hip_demo::geometry::ellipsoid {
struct Description {
    scene_ast::Position where;
    scene_ast::Vector center, radii;
    std::string material;
};
}
namespace scene_ast { using Ellipsoid = hip_demo::geometry::ellipsoid::Description; }
