#pragma once

#include "components/value/parser.h"
#include "components/texture/parser.h"
#include "components/material/glass/description.h"

namespace hip_demo::parsing {
struct glass_p_;

}
namespace hip_demo::material::glass::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::glass_p_, Description> glass_p = "glass material";

inline auto glass_p_def = bp::lit("glass") > vector_p > -(keyword("texture") > material_texture_p);
}
namespace hip_demo::parsing {
using hip_demo::material::glass::parsing::glass_p;
using hip_demo::material::glass::parsing::glass_p_def;
}
