#pragma once

#include "scene/common.h"

namespace hip_demo::camera {

struct Model {
    SceneVector position;
    SceneVector target;
    float half_view_height = 0;
};

}
