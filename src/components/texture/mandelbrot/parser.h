#pragma once

#include "components/value/parser.h"
#include "components/texture/mandelbrot/description.h"

namespace hip_demo::parsing { struct mandelbrot_p_; }
namespace hip_demo::texture::mandelbrot::parsing {
}
namespace hip_demo::texture::mandelbrot::parsing {
using namespace hip_demo::parsing;
inline bp::rule<hip_demo::parsing::mandelbrot_p_, Description> mandelbrot_p = "Mandelbrot surface";
inline auto mandelbrot_p_def = keyword("mandelbrot_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using hip_demo::texture::mandelbrot::parsing::mandelbrot_p;
using hip_demo::texture::mandelbrot::parsing::mandelbrot_p_def;
}
