#pragma once

#include "scene/common.h"

namespace hip_demo::color {

struct Model { SceneVector color; };
}
namespace hip_demo { using ConstantColor = color::Model; }
