#pragma once

#include "components/rectangle_light/parser.h"
#include "components/torus_light/parser.h"
#include "components/light/description.h"

namespace hip_demo::parsing {
struct light_p_;

}
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
inline bp::rule<hip_demo::parsing::light_p_, Light> light_p = "light";

inline auto light_p_def = keyword("light") > (rectangle_light_p | torus_light_p);
}
namespace hip_demo::parsing {
using light::parsing::light_p;
using light::parsing::light_p_def;
}
