#pragma once

#include "components/value/parser.h"
#include "components/geometry/torus/description.h"

namespace hip_demo::parsing {
struct torus_p_;
struct csg_torus_p_;

}
namespace hip_demo::geometry::torus::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double major_radius, minor_radius, tilt_x;
    std::string material;
};
}
namespace hip_demo::parsing {
using Torus = hip_demo::geometry::torus::parsing::Parsed;
}
namespace hip_demo::geometry::torus::parsing {
using hip_demo::parsing::Torus;
inline bp::rule<hip_demo::parsing::torus_p_, Torus> torus_p = "torus";
inline bp::rule<hip_demo::parsing::csg_torus_p_, Torus> csg_torus_p = "CSG torus";

inline auto torus_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > keyword("tilt_x") > scalar_p
    > keyword("material") > identifier_p > '}';
inline auto csg_torus_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > keyword("tilt_x") > scalar_p > bp::attr(std::string{}) > '}';
}
namespace hip_demo::parsing {
using hip_demo::geometry::torus::parsing::torus_p;
using hip_demo::geometry::torus::parsing::torus_p_def;
using hip_demo::geometry::torus::parsing::csg_torus_p;
using hip_demo::geometry::torus::parsing::csg_torus_p_def;
}
