#pragma once

#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo {

struct LightSample {
    float3 point;
    float3 normal;
    float area_weight;
};

}
