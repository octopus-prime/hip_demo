#pragma once

#include "components/value/parser.h"
#include "components/material/diffuse/description.h"

namespace hip_demo::parsing {
struct diffuse_p_;

}
namespace hip_demo::material::diffuse::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::diffuse_p_, Description> diffuse_p = "diffuse material";

inline auto diffuse_p_def = bp::lit("diffuse") > vector_p;
}
namespace hip_demo::parsing {
using hip_demo::material::diffuse::parsing::diffuse_p;
using hip_demo::material::diffuse::parsing::diffuse_p_def;
}
