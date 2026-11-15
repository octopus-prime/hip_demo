#pragma once

#include "components/value/parser.h"
#include "components/texture/color/description.h"

namespace hip_demo::parsing {
struct surface_p_;

}
namespace hip_demo::texture::color::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::surface_p_, Description> surface_p = "color surface";

inline auto surface_p_def = bp::lit("color") > vector_p;
}
namespace hip_demo::parsing {
using hip_demo::texture::color::parsing::surface_p;
using hip_demo::texture::color::parsing::surface_p_def;
}
