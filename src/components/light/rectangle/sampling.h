#pragma once
#include "gpu/scene_view.h"

#include "light/types.h"

namespace hip_demo {

__device__ inline LightSample sample_rectangle_light(Random& random, const GpuEmitter& emitter, TraceScene scene) {
    const auto vertex = [&] __device__ (unsigned int index) {
        const auto v = scene.mesh_vertices[index];
        return float3{v.x, v.y, v.z};
    };
    const auto a = vertex(emitter.first * 3);
    const auto u = vertex(emitter.first * 3 + 1) - a;
    const auto v = vertex(emitter.first * 3 + 2) - a;
    return {a + random.next() * u + random.next() * v, normalize(cross(u, v)),
            static_cast<float>(1 / emitter.area), {}, emitter.object_id, emitter.first};
}

}
