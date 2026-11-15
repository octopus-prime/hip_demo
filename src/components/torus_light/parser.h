#pragma once

#include "components/value/parser.h"
#include "components/torus_light/description.h"

namespace hip_demo::parsing {
struct torus_light_p_;

}
namespace hip_demo::torus_light::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double major_radius, minor_radius;
    Vector radiance;
};
}
namespace hip_demo::parsing {
using TorusLight = torus_light::parsing::Parsed;
}
namespace hip_demo::torus_light::parsing {
using hip_demo::parsing::TorusLight;
inline bp::rule<hip_demo::parsing::torus_light_p_, TorusLight> torus_light_p = "torus light";

inline auto torus_light_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > bp::lit("radiance") > vector_p > '}';
}
namespace hip_demo::parsing {
using torus_light::parsing::torus_light_p;
using torus_light::parsing::torus_light_p_def;
}
