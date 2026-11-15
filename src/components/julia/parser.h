#pragma once

#include "components/value/parser.h"
#include "components/julia/description.h"

namespace hip_demo::parsing { struct julia_p_; }
namespace hip_demo::julia::parsing {
}
namespace hip_demo::julia::parsing {
using namespace hip_demo::parsing;
inline bp::rule<hip_demo::parsing::julia_p_, Description> julia_p = "Julia surface";
inline auto julia_p_def = keyword("julia_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using julia::parsing::julia_p;
using julia::parsing::julia_p_def;
}
