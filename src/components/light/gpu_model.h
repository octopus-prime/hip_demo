#pragma once

#include "scene/common.h"

struct SceneLight {
    SceneVector center;
    SceneVector radiance;
    float width = 0, depth = 0;
    float major_radius = 0, minor_radius = 0;
    bool torus = false;
};
