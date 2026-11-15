#pragma once

#include "texture/dispatch.h"

namespace hip_demo {

__device__ inline void shade_room(Hit& hit, const Ray& ray, int wall, bool detail, const GpuSceneSettings* settings) {
    const float3 point = hit.point;
    hit.albedo = wall == 3 ? float3{0.72f, 0.10f, 0.10f} :
        wall == 4 ? float3{0.10f, 0.65f, 0.16f} : float3{0.75f, 0.75f, 0.75f};
    const auto texture = settings ? settings->textures[wall].kind :
        !detail ? TextureKind::color : wall == 0 ? TextureKind::tiles :
        wall == 1 ? TextureKind::wood : wall == 2 ? TextureKind::marble : wall == 3 ? TextureKind::mandelbrot :
        wall == 4 ? TextureKind::julia : TextureKind::color;
    if (settings) hit.albedo = vector3(settings->textures[wall].color);
    hit.texture = texture;
    apply_texture(hit, ray, settings, wall);
    const bool on_light = settings
        ? !settings->light.torus &&
          fabsf(point.x - settings->light.center.x) <= settings->light.width / 2 &&
          fabsf(point.z - settings->light.center.z) <= settings->light.depth / 2
        : !detail && fabsf(point.x) <= light_half_width &&
          fabsf(point.z - light_center_z) <= light_half_depth;
    hit.material = wall == 1 && on_light ? ShaderKind::emissive : ShaderKind::diffuse;
    hit.roughness = 1.0f;
}

}
