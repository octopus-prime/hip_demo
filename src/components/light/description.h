#pragma once

#include "components/light/rectangle/description.h"
#include "components/light/torus/description.h"
#include <variant>

namespace hip_demo::light {
using Description = std::variant<hip_demo::light::rectangle::Description, hip_demo::light::torus::Description>;
}
namespace scene_ast { using Light = hip_demo::light::Description; }
