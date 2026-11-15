#pragma once
#include "components/value/parser.h"
#include "components/geometry/ellipsoid/description.h"

namespace hip_demo::parsing { struct ellipsoid_p_; struct csg_ellipsoid_p_; }
namespace hip_demo::geometry::ellipsoid::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center, radii;
    std::string material;
};
inline bp::rule<hip_demo::parsing::ellipsoid_p_, Parsed> ellipsoid_p = "ellipsoid";
inline bp::rule<hip_demo::parsing::csg_ellipsoid_p_, Parsed> csg_ellipsoid_p = "CSG ellipsoid";
inline auto ellipsoid_p_def = position_p >> bp::lit("ellipsoid") > '{'
    > bp::lit("center") > vector_p > keyword("radii") > vector_p
    > keyword("material") > identifier_p > '}';
inline auto csg_ellipsoid_p_def = position_p >> bp::lit("ellipsoid") > '{'
    > bp::lit("center") > vector_p > keyword("radii") > vector_p > bp::attr(std::string{}) > '}';
}
namespace hip_demo::parsing {
using Ellipsoid = geometry::ellipsoid::parsing::Parsed;
using geometry::ellipsoid::parsing::ellipsoid_p;
using geometry::ellipsoid::parsing::ellipsoid_p_def;
using geometry::ellipsoid::parsing::csg_ellipsoid_p;
using geometry::ellipsoid::parsing::csg_ellipsoid_p_def;
}
