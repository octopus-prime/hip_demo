#pragma once

#include "components/texture/color/model.h"
#include "components/texture/tiles/model.h"
#include "components/texture/wood/model.h"
#include "components/texture/mandelbrot/model.h"
#include "components/texture/julia/model.h"
#include "components/texture/marble/model.h"
#include <variant>

namespace hip_demo::texture {
using Model = std::variant<ConstantColor, Tiles, Wood, Mandelbrot, Julia, Marble>;
}
namespace hip_demo { using Texture = texture::Model; }
