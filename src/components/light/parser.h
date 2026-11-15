#pragma once

#include "components/light/rectangle/parser.h"
#include "components/light/torus/parser.h"
#include "components/light/description.h"

namespace hip_demo::light::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
using hip_demo::parsing::RectangleLight;
using hip_demo::parsing::TorusLight;
using Parsed = std::variant<RectangleLight, TorusLight>;
}
namespace hip_demo::parsing {
using Light = light::parsing::Parsed;
}
namespace hip_demo::light::parsing {
using hip_demo::parsing::Light;
inline auto light_p = keyword("light") > (rectangle_light_p | torus_light_p);
}
namespace hip_demo::parsing {
using light::parsing::light_p;
}
