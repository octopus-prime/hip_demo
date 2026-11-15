#pragma once

#include "components/value/parser.h"
#include "components/texture/tiles/description.h"

namespace hip_demo::parsing { struct tiles_p_; }
namespace hip_demo::texture::tiles::parsing {
}
namespace hip_demo::texture::tiles::parsing {
using namespace hip_demo::parsing;
inline bp::rule<hip_demo::parsing::tiles_p_, Description> tiles_p = "tile surface";
inline auto tiles_p_def = keyword("tiles_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using hip_demo::texture::tiles::parsing::tiles_p;
using hip_demo::texture::tiles::parsing::tiles_p_def;
}
