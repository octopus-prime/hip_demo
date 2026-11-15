#pragma once

#include "components/mandelbrot/description.h"
#include "components/mandelbrot/model.h"

namespace hip_demo::mandelbrot {
inline Model build(const Description&) { return {}; }
}
