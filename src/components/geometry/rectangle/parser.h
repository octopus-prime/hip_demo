#pragma once

#include "components/value/parser.h"
#include "components/geometry/rectangle/description.h"

namespace hip_demo::parsing { struct rectangle_p_; }
namespace hip_demo::geometry::rectangle::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center;
    double width, depth;
    std::string material;
};
inline bp::rule<hip_demo::parsing::rectangle_p_, Parsed> rectangle_p = "rectangle";
inline auto rectangle_p_def = position_p >> bp::lit("rectangle") > '{'
    > bp::lit("center") > vector_p > keyword("width") > scalar_p > keyword("depth") > scalar_p
    > keyword("material") > identifier_p > '}';
}
namespace hip_demo::parsing {
using Rectangle = geometry::rectangle::parsing::Parsed;
using geometry::rectangle::parsing::rectangle_p;
using geometry::rectangle::parsing::rectangle_p_def;
}
