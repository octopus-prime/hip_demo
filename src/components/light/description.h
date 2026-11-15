#pragma once

#include "components/rectangle_light/description.h"
#include "components/torus_light/description.h"
#include <variant>

namespace hip_demo::light {
using Description = std::variant<rectangle_light::Description, torus_light::Description>;
}
namespace scene_ast { using Light = hip_demo::light::Description; }
