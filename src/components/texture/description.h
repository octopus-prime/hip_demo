#pragma once

#include "components/texture/color/description.h"
#include "components/texture/tiles/description.h"
#include "components/texture/wood/description.h"
#include "components/texture/mandelbrot/description.h"
#include "components/texture/julia/description.h"
#include "components/texture/marble/description.h"
#include <variant>

namespace hip_demo::texture {

using Description = std::variant<hip_demo::texture::color::Description, hip_demo::texture::tiles::Description, hip_demo::texture::wood::Description,
    hip_demo::texture::mandelbrot::Description, hip_demo::texture::julia::Description, hip_demo::texture::marble::Description>;
}
namespace scene_ast { using TextureValue = hip_demo::texture::Description; }
