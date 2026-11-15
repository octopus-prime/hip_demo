#pragma once

#include "components/value/parser.h"
#include "components/texture/parser.h"
#include "components/material/metal/description.h"

namespace hip_demo::parsing {
struct metal_p_;

}
namespace hip_demo::material::metal::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::metal_p_, Description> metal_p = "metal material";

inline auto metal_p_def = bp::lit("metal") > vector_p > keyword("roughness") > scalar_p
    > -(keyword("texture") > material_texture_p);
}
namespace hip_demo::parsing {
using hip_demo::material::metal::parsing::metal_p;
using hip_demo::material::metal::parsing::metal_p_def;
}
