#pragma once
#include "components/value/parser.h"
#include "components/geometry/cone/description.h"

namespace hip_demo::parsing { struct cone_p_; struct csg_cone_p_; }
namespace hip_demo::geometry::cone::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double radius, height;
    std::string material;
};
inline bp::rule<hip_demo::parsing::cone_p_, Parsed> cone_p = "cone";
inline bp::rule<hip_demo::parsing::csg_cone_p_, Parsed> csg_cone_p = "CSG cone";
inline auto cone_p_def = position_p >> bp::lit("cone") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > keyword("material") > identifier_p > '}';
inline auto csg_cone_p_def = position_p >> bp::lit("cone") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > bp::attr(std::string{}) > '}';
}
namespace hip_demo::parsing {
using Cone = geometry::cone::parsing::Parsed;
using geometry::cone::parsing::cone_p;
using geometry::cone::parsing::cone_p_def;
using geometry::cone::parsing::csg_cone_p;
using geometry::cone::parsing::csg_cone_p_def;
}
