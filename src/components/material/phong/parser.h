#pragma once

#include "components/value/parser.h"
#include "components/texture/parser.h"
#include "components/material/phong/description.h"

namespace hip_demo::parsing {
struct phong_p_;

}
namespace hip_demo::material::phong::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::phong_p_, Description> phong_p = "Phong material";

inline auto phong_p_def = bp::lit("phong") > vector_p > -(keyword("texture") > material_texture_p);
}
namespace hip_demo::parsing {
using hip_demo::material::phong::parsing::phong_p;
using hip_demo::material::phong::parsing::phong_p_def;
}
