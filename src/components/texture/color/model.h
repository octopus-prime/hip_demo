#pragma once

#include "scene/common.h"

namespace hip_demo::texture::color {

struct Model { SceneVector color; };
}
namespace hip_demo { using ConstantColor = hip_demo::texture::color::Model; }
