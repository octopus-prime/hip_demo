#pragma once

#include "components/value/parser.h"
#include "components/material/emissive/description.h"

namespace hip_demo::parsing { struct emissive_p_; }
namespace hip_demo::material::emissive::parsing {
using namespace hip_demo::parsing;
inline bp::rule<hip_demo::parsing::emissive_p_, Description> emissive_p = "emissive material";
inline auto emissive_p_def = bp::lit("emissive") > vector_p;
}
namespace hip_demo::parsing {
using material::emissive::parsing::emissive_p;
using material::emissive::parsing::emissive_p_def;
}
