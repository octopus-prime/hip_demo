#pragma once

#include "light/types.h"

namespace hip_demo {

__device__ inline LightSample sample_rectangle_light(Random& random, const SceneLight& source) {
    return {{source.center.x + (random.next() - 0.5f) * source.width,
             source.center.y,
             source.center.z + (random.next() - 0.5f) * source.depth},
            {0.0f, -1.0f, 0.0f}, source.width * source.depth};
}

}
