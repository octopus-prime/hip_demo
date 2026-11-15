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

__device__ inline double light_pdf(const Ray& ray, const Hit& hit, TraceScene scene) {
    if (!hit.sampled_emitter) return 0;
    for (unsigned int index = 0; index < scene.emitter_count; ++index) {
        const auto& emitter = scene.emitters[index];
        if (emitter.object_id != hit.object_id) continue;
        float area_pdf = 0;
        switch (emitter.kind) {
        case EmitterKind::torus: {
            const auto& torus = scene.primitives[emitter.first].geometry.torus;
            const float3 local = rotate_x(hit.point - vector3(torus.center), -torus.tilt_sine, torus.tilt_cosine);
            area_pdf = torus_light_area_pdf(torus, sqrtf(local.x * local.x + local.z * local.z));
            break;
        }
        case EmitterKind::rectangle:
        case EmitterKind::mesh:
            area_pdf = static_cast<float>(1 / emitter.area);
            break;
        default:
            __builtin_trap();
        }
        area_pdf /= scene.emitter_count;
        const float3 offset = hit.point - ray.origin;
        const double distance_squared = double(offset.x) * offset.x + double(offset.y) * offset.y +
                                        double(offset.z) * offset.z;
        return solid_angle_pdf(area_pdf, distance_squared, dot(hit.geometric_normal, -ray.direction));
    }
    __builtin_trap();
}
}
