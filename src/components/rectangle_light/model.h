#pragma once

#include "scene/common.h"

namespace hip_demo::rectangle_light {

struct Model {
    SceneVector center;
    SceneVector radiance;
    float width = 0, depth = 0;
};

}
