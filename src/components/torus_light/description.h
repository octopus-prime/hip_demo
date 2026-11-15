#pragma once

#include "components/value/description.h"

namespace hip_demo::torus_light {

struct Description {
    scene_ast::Position where;
    scene_ast::Vector center;
    double major_radius, minor_radius;
    scene_ast::Vector radiance;
};
}
namespace scene_ast { using TorusLight = hip_demo::torus_light::Description; }
