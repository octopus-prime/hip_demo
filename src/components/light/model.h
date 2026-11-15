#pragma once

#include "components/rectangle_light/model.h"
#include "components/torus_light/model.h"
#include <variant>

namespace hip_demo::light {

using Model = std::variant<rectangle_light::Model, torus_light::Model>;

}
