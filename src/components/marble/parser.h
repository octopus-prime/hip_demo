#pragma once

#include "components/value/parser.h"
#include "components/marble/description.h"

namespace hip_demo::parsing {
struct marble_p_;

}
namespace hip_demo::marble::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::marble_p_, Description> marble_p = "marble surface";

inline auto marble_p_def = keyword("marble_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using marble::parsing::marble_p;
using marble::parsing::marble_p_def;
}
