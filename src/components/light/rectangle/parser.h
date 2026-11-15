#pragma once

#include "components/value/parser.h"
#include "components/light/rectangle/description.h"

namespace hip_demo::parsing {
struct rectangle_light_p_;

}
namespace hip_demo::light::rectangle::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    std::array<double, 2> center, size;
    Vector radiance;
};
}
namespace hip_demo::parsing {
using RectangleLight = hip_demo::light::rectangle::parsing::Parsed;
}
namespace hip_demo::light::rectangle::parsing {
using hip_demo::parsing::RectangleLight;
inline bp::rule<hip_demo::parsing::rectangle_light_p_, RectangleLight> rectangle_light_p = "ceiling rectangle light";

inline auto rectangle_light_p_def = position_p >> bp::lit("ceiling_rectangle") > '{'
    > bp::lit("center") > vector2_p > bp::lit("size") > vector2_p
    > bp::lit("radiance") > vector_p > '}';
}
namespace hip_demo::parsing {
using hip_demo::light::rectangle::parsing::rectangle_light_p;
using hip_demo::light::rectangle::parsing::rectangle_light_p_def;
}
