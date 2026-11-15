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

__device__ inline double solid_angle_pdf(float area_pdf, double distance_squared, float light_cosine) {
    return light_cosine > 0 ? double(area_pdf) * distance_squared / light_cosine : 0;
}

}
