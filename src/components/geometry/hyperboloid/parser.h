#pragma once
#include "components/value/parser.h"
#include "components/geometry/hyperboloid/description.h"

namespace hip_demo::parsing { struct hyperboloid_p_; struct csg_hyperboloid_p_; }
namespace hip_demo::geometry::hyperboloid::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double radius, axial_radius, height;
    std::string material;
};
inline bp::rule<hip_demo::parsing::hyperboloid_p_, Parsed> hyperboloid_p = "hyperboloid";
inline bp::rule<hip_demo::parsing::csg_hyperboloid_p_, Parsed> csg_hyperboloid_p = "CSG hyperboloid";
inline auto hyperboloid_p_def = position_p >> bp::lit("hyperboloid") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("axial_radius") > scalar_p
    > keyword("height") > scalar_p > keyword("material") > identifier_p > '}';
inline auto csg_hyperboloid_p_def = position_p >> bp::lit("hyperboloid") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("axial_radius") > scalar_p
    > keyword("height") > scalar_p     > optional_material_reference_p > '}';
}
namespace hip_demo::parsing {
using Hyperboloid = geometry::hyperboloid::parsing::Parsed;
using geometry::hyperboloid::parsing::hyperboloid_p;
using geometry::hyperboloid::parsing::hyperboloid_p_def;
using geometry::hyperboloid::parsing::csg_hyperboloid_p;
using geometry::hyperboloid::parsing::csg_hyperboloid_p_def;
}
