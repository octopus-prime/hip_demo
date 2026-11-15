#pragma once

#include "components/value/parser.h"
#include "components/geometry/sphere/description.h"

namespace hip_demo::parsing {
struct sphere_p_;
struct csg_sphere_p_;

}
namespace hip_demo::geometry::sphere::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double radius;
    std::string material;
};
}
namespace hip_demo::parsing {
using Sphere = hip_demo::geometry::sphere::parsing::Parsed;
}
namespace hip_demo::geometry::sphere::parsing {
using hip_demo::parsing::Sphere;
inline bp::rule<hip_demo::parsing::sphere_p_, Sphere> sphere_p = "sphere";
inline bp::rule<hip_demo::parsing::csg_sphere_p_, Sphere> csg_sphere_p = "CSG sphere";

inline auto sphere_p_def = position_p >> bp::lit("sphere") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p
    > keyword("material") > identifier_p > '}';
inline auto csg_sphere_p_def = position_p >> bp::lit("sphere") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > optional_material_reference_p > '}';
}
namespace hip_demo::parsing {
using hip_demo::geometry::sphere::parsing::sphere_p;
using hip_demo::geometry::sphere::parsing::sphere_p_def;
using hip_demo::geometry::sphere::parsing::csg_sphere_p;
using hip_demo::geometry::sphere::parsing::csg_sphere_p_def;
}
