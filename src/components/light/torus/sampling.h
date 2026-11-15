#pragma once
#include "components/light/gpu_model.h"

#include "light/types.h"

namespace hip_demo {

__device__ inline LightSample sample_torus_light(Random& random, const SceneLight* light = nullptr) {
    const float major = light ? light->major_radius : detail_torus_major_radius;
    const float minor = light ? light->minor_radius : detail_torus_minor_radius;
    const float3 center = light ? vector3(light->center) : detail_torus_center;
    const float around = 2.0f * pi * random.next();
    const float tube = 2.0f * pi * random.next();
    const float radial = major + minor * cosf(tube);
    const float3 normal{cosf(tube) * cosf(around), sinf(tube), cosf(tube) * sinf(around)};
    return {center + float3{radial * cosf(around),
                                         minor * sinf(tube),
                                         radial * sinf(around)},
            normal, 4.0f * pi * pi * minor * radial};
}

}
