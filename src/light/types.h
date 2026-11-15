#pragma once

#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo {

struct LightSample {
    float3 point;
    float3 normal;
    float area_pdf;
    float3 radiance;
    unsigned int object_id;
    unsigned int triangle;
};

}
