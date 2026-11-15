#pragma once

#include "components/value/parser.h"
#include "components/mandelbrot/description.h"

namespace hip_demo::parsing { struct mandelbrot_p_; }
namespace hip_demo::mandelbrot::parsing {
}
namespace hip_demo::mandelbrot::parsing {
using namespace hip_demo::parsing;
inline bp::rule<hip_demo::parsing::mandelbrot_p_, Description> mandelbrot_p = "Mandelbrot surface";
inline auto mandelbrot_p_def = keyword("mandelbrot_detail") >> bp::attr(Description{});
}
namespace hip_demo::parsing {
using mandelbrot::parsing::mandelbrot_p;
using mandelbrot::parsing::mandelbrot_p_def;
}
