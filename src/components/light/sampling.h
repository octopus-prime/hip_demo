#pragma once

#include "components/light/rectangle/sampling.h"
#include "components/light/torus/sampling.h"
#include "components/light/mesh/sampling.h"
#include "texture/dispatch.h"

namespace hip_demo {
__device__ inline LightSample sample_light(Random& random, TraceScene scene) {
    unsigned int index = 0;
    if (scene.emitter_count > 1)
        index = static_cast<unsigned int>(double(random.next()) * scene.emitter_count);
    const auto& emitter = scene.emitters[index];
    LightSample sample{};
    GpuMaterial material{};
    switch (emitter.kind) {
    case EmitterKind::torus:
        sample = sample_torus_light(random, scene.primitives[emitter.first].geometry.torus);
        material = scene.primitives[emitter.first].material;
        break;
    case EmitterKind::rectangle:
        sample = sample_rectangle_light(random, emitter, scene);
        material = scene.triangle_materials[sample.triangle].material;
        break;
    case EmitterKind::mesh:
        sample = sample_mesh_light(random, emitter, scene);
        material = scene.triangle_materials[sample.triangle].material;
        break;
    default:
        __builtin_trap();
    }
    sample.object_id = emitter.object_id;
    sample.area_pdf /= scene.emitter_count;
    Hit surface{};
    surface.point = sample.point;
    surface.normal = surface.geometric_normal = sample.normal;
    set_material(surface, material);
    apply_texture(surface, {sample.point, -sample.normal}, scene.settings);
    sample.radiance = surface.emission;
    return sample;
}
}
