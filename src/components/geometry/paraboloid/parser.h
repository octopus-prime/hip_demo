#pragma once
#include "components/value/parser.h"
#include "components/geometry/paraboloid/description.h"

namespace hip_demo::parsing { struct paraboloid_p_; struct csg_paraboloid_p_; }
namespace hip_demo::geometry::paraboloid::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double radius, height;
    std::string material;
};
inline bp::rule<hip_demo::parsing::paraboloid_p_, Parsed> paraboloid_p = "paraboloid";
inline bp::rule<hip_demo::parsing::csg_paraboloid_p_, Parsed> csg_paraboloid_p = "CSG paraboloid";
inline auto paraboloid_p_def = position_p >> bp::lit("paraboloid") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > keyword("material") > identifier_p > '}';
inline auto csg_paraboloid_p_def = position_p >> bp::lit("paraboloid") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > bp::attr(std::string{}) > '}';
}
namespace hip_demo::parsing {
using Paraboloid = geometry::paraboloid::parsing::Parsed;
using geometry::paraboloid::parsing::paraboloid_p;
using geometry::paraboloid::parsing::paraboloid_p_def;
using geometry::paraboloid::parsing::csg_paraboloid_p;
using geometry::paraboloid::parsing::csg_paraboloid_p_def;
}
