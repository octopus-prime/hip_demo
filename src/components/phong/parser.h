#pragma once

#include "components/value/parser.h"
#include "components/phong/description.h"

namespace hip_demo::parsing {
struct phong_p_;

}
namespace hip_demo::phong::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::phong_p_, Description> phong_p = "Phong material";

inline auto phong_p_def = bp::lit("phong") > vector_p;
}
namespace hip_demo::parsing {
using phong::parsing::phong_p;
using phong::parsing::phong_p_def;
}
