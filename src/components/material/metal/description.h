#pragma once

#include "components/value/description.h"

namespace hip_demo::material::metal {
struct Description {
    scene_ast::Vector albedo;
    double roughness = 1;
};
}
