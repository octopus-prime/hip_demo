#pragma once

#include "components/color/model.h"
#include "components/tiles/model.h"
#include "components/wood/model.h"
#include "components/mandelbrot/model.h"
#include "components/julia/model.h"
#include "components/marble/model.h"
#include <variant>

namespace hip_demo::texture {
using Model = std::variant<ConstantColor, Tiles, Wood, Mandelbrot, Julia, Marble>;
}
namespace hip_demo { using Texture = texture::Model; }
