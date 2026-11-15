#pragma once

#include "scene/common.h"

namespace hip_demo::torus_light {

struct Model {
    SceneVector center;
    SceneVector radiance;
    float major_radius = 0, minor_radius = 0;
};

}
