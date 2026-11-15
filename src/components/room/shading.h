#pragma once

#include "texture/dispatch.h"

namespace hip_demo {

__device__ inline void shade_room(Hit& hit, const Ray& ray, int wall, bool detail, const GpuSceneSettings* settings) {
    hit.albedo = wall == 3 ? float3{0.72f, 0.10f, 0.10f} :
        wall == 4 ? float3{0.10f, 0.65f, 0.16f} : float3{0.75f, 0.75f, 0.75f};
    const auto texture = settings ? settings->textures[wall].kind :
        !detail ? TextureKind::color : wall == 0 ? TextureKind::tiles :
        wall == 1 ? TextureKind::wood : wall == 2 ? TextureKind::marble : wall == 3 ? TextureKind::mandelbrot :
        wall == 4 ? TextureKind::julia : TextureKind::color;
    if (settings) hit.albedo = vector3(settings->textures[wall].color);
    hit.texture = texture;
    hit.material = ShaderKind::diffuse;
    hit.sampled_emitter = false;
    hit.roughness = 1.0f;
    apply_texture(hit, ray, settings, wall);
}

}
