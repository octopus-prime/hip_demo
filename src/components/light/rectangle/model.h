#pragma once

#include "scene/common.h"

namespace hip_demo::light::rectangle {

struct Model {
    SceneVector center;
    SceneVector radiance;
    float width = 0, depth = 0;
};

}
