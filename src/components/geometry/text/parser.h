#pragma once

#include "components/value/parser.h"
#include "components/geometry/text/description.h"

namespace hip_demo::parsing {
struct text_p_;
struct csg_text_p_;

}
namespace hip_demo::geometry::text::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    std::string font, value;
    double size, depth;
    Vector position;
    std::string material;
};
}
namespace hip_demo::parsing {
using Text = hip_demo::geometry::text::parsing::Parsed;
}
namespace hip_demo::geometry::text::parsing {
using hip_demo::parsing::Text;
inline bp::rule<hip_demo::parsing::text_p_, Text> text_p = "extruded text";
inline bp::rule<hip_demo::parsing::csg_text_p_, Text> csg_text_p = "CSG text";

inline auto text_p_def = position_p >> bp::lit("text") > '{'
    > bp::lit("font") > string_p > bp::lit("value") > string_p
    > keyword("size") > scalar_p > keyword("depth") > scalar_p
    > bp::lit("position") > vector_p > keyword("material") > identifier_p > '}';
inline auto csg_text_p_def = position_p >> bp::lit("text") > '{'
    > bp::lit("font") > string_p > bp::lit("value") > string_p
    > keyword("size") > scalar_p > keyword("depth") > scalar_p
    > bp::lit("position") > vector_p > optional_material_reference_p > '}';
}
namespace hip_demo::parsing {
using hip_demo::geometry::text::parsing::text_p;
using hip_demo::geometry::text::parsing::text_p_def;
using hip_demo::geometry::text::parsing::csg_text_p;
using hip_demo::geometry::text::parsing::csg_text_p_def;
}
