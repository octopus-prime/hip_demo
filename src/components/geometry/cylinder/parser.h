#pragma once
#include "components/value/parser.h"
#include "components/geometry/cylinder/description.h"

namespace hip_demo::parsing { struct cylinder_p_; struct csg_cylinder_p_; }
namespace hip_demo::geometry::cylinder::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double radius, height;
    Axis axis = Axis::y;
    std::string material;
};
inline bp::rule<hip_demo::parsing::cylinder_p_, Parsed> cylinder_p = "cylinder";
inline bp::rule<hip_demo::parsing::csg_cylinder_p_, Parsed> csg_cylinder_p = "CSG cylinder";
inline auto cylinder_axis_p = (keyword("axis") >
    ((keyword("x") >> bp::attr(Axis::x)) | (keyword("y") >> bp::attr(Axis::y)) |
     (keyword("z") >> bp::attr(Axis::z)))) | bp::attr(Axis::y);
inline auto cylinder_p_def = position_p >> bp::lit("cylinder") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > cylinder_axis_p > keyword("material") > identifier_p > '}';
inline auto csg_cylinder_p_def = position_p >> bp::lit("cylinder") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > keyword("height") > scalar_p
    > cylinder_axis_p > optional_material_reference_p > '}';
}
namespace hip_demo::parsing {
using Cylinder = geometry::cylinder::parsing::Parsed;
using geometry::cylinder::parsing::cylinder_p;
using geometry::cylinder::parsing::cylinder_p_def;
using geometry::cylinder::parsing::csg_cylinder_p;
using geometry::cylinder::parsing::csg_cylinder_p_def;
}
