#pragma once

#include "components/light/rectangle/model.h"
#include "components/light/torus/model.h"
#include <variant>

namespace hip_demo::light {

using Model = std::variant<hip_demo::light::rectangle::Model, hip_demo::light::torus::Model>;

}
