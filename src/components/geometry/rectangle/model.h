#pragma once

#include "scene/common.h"

namespace hip_demo::geometry::rectangle {
struct Model {
    SceneVector center;
    float width = 0, depth = 0;
};
}
namespace hip_demo { using Rectangle = geometry::rectangle::Model; }
