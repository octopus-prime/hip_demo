#pragma once

#include "gpu/scene_view.h"
#include "light/types.h"

namespace hip_demo {
__device__ inline LightSample sample_mesh_light(Random& random, const GpuEmitter& emitter, TraceScene scene) {
    const double target = double(random.next()) * emitter.area;
    unsigned int low = 0, high = emitter.count;
    while (low + 1 < high) {
        const auto middle = low + (high - low) / 2;
        if (target < scene.emitter_triangles[emitter.first + middle - 1].cumulative_area) high = middle;
        else low = middle;
    }
    const auto triangle = scene.emitter_triangles[emitter.first + low].triangle;
    const auto vertex = [&] __device__ (unsigned int index) {
        const auto v = scene.mesh_vertices[index];
        return float3{v.x, v.y, v.z};
    };
    const auto a = vertex(triangle * 3), b = vertex(triangle * 3 + 1), c = vertex(triangle * 3 + 2);
    const float root = sqrtf(random.next()), along = random.next();
    return {(1 - root) * a + root * (1 - along) * b + root * along * c,
            normalize(cross(b - a, c - a)), static_cast<float>(1 / emitter.area),
            {}, emitter.object_id, triangle};
}
}
