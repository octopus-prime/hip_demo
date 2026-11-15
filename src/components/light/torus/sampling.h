#pragma once
#include "components/geometry/torus/model.h"

#include "light/types.h"

namespace hip_demo {

__device__ inline float torus_light_area_pdf(const Torus& torus, float radial) {
    return static_cast<float>(1.0 / (4.0 * pi * pi * torus.minor_radius * radial));
}

__device__ inline LightSample sample_torus_light(Random& random, const Torus& torus) {
    const float major = torus.major_radius;
    const float minor = torus.minor_radius;
    const float around = 2.0f * pi * random.next();
    const float tube = 2.0f * pi * random.next();
    const float radial = major + minor * cosf(tube);
    const float3 normal{cosf(tube) * cosf(around), sinf(tube), cosf(tube) * sinf(around)};
    const float3 local{radial * cosf(around), minor * sinf(tube), radial * sinf(around)};
    return {vector3(torus.center) + rotate_x(local, torus.tilt_sine, torus.tilt_cosine),
            rotate_x(normal, torus.tilt_sine, torus.tilt_cosine),
            torus_light_area_pdf(torus, radial), {}, 0, 0};
}

}
