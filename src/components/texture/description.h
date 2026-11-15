#pragma once

#include "components/color/description.h"
#include "components/tiles/description.h"
#include "components/wood/description.h"
#include "components/mandelbrot/description.h"
#include "components/julia/description.h"
#include "components/marble/description.h"
#include <variant>

namespace hip_demo::texture {

using Description = std::variant<color::Description, tiles::Description, wood::Description,
    mandelbrot::Description, julia::Description, marble::Description>;
}
namespace scene_ast { using TextureValue = hip_demo::texture::Description; }
