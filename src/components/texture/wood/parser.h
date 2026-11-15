#pragma once

#include "components/value/parser.h"
#include "components/texture/wood/description.h"

namespace hip_demo::parsing {
struct wood_p_;

}
namespace hip_demo::texture::wood::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::wood_p_, Description> wood_p = "wood surface";

inline auto wood_p_def = keyword("wood_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using hip_demo::texture::wood::parsing::wood_p;
using hip_demo::texture::wood::parsing::wood_p_def;
}
