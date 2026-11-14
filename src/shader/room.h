#pragma once

#include "texture/tiles.h"
#include "texture/wood.h"
#include "texture/marble.h"
#include "texture/mandelbrot.h"
#include "texture/julia.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline void shade_room(Hit& hit, const Ray& ray, int wall, bool detail, const SceneSettings* settings) {
    const float3 point = hit.point;
    const float half_depth = settings ? settings->room_size.z / 2 : room_half_depth;
    const float height = settings ? settings->room_size.y : room_height;
    hit.albedo = wall == 3 ? float3{0.72f, 0.10f, 0.10f} :
        wall == 4 ? float3{0.10f, 0.65f, 0.16f} : float3{0.75f, 0.75f, 0.75f};
    const auto surface = settings ? settings->surfaces[wall].kind :
        !detail ? SurfaceKind::color : wall == 0 ? SurfaceKind::tiles :
        wall == 1 ? SurfaceKind::wood : wall == 2 ? SurfaceKind::marble : wall == 3 ? SurfaceKind::mandelbrot :
        wall == 4 ? SurfaceKind::julia : SurfaceKind::color;
    if (settings) hit.albedo = vector3(settings->surfaces[wall].color);
    if (surface == SurfaceKind::tiles) {
        apply_floor_tiles(hit, ray);
    } else if (surface == SurfaceKind::wood) {
        hit.albedo = wood_albedo(wall == 1 ? float3{point.x, point.z, 0.0f} : point);
    } else if (surface == SurfaceKind::marble) {
        hit.albedo = marble_albedo(point);
    } else if (surface == SurfaceKind::mandelbrot || surface == SurfaceKind::julia) {
        hit.albedo = surface == SurfaceKind::julia
            ? julia_albedo(point, 2 * half_depth, height) : mandelbrot_albedo(point, 2 * half_depth, height);
    }
    const bool on_light = settings
        ? !settings->light.torus &&
          fabsf(point.x - settings->light.center.x) <= settings->light.width / 2 &&
          fabsf(point.z - settings->light.center.z) <= settings->light.depth / 2
        : !detail && fabsf(point.x) <= light_half_width &&
          fabsf(point.z - light_center_z) <= light_half_depth;
    hit.material = wall == 1 && on_light ? Material::emissive : Material::diffuse;
    hit.roughness = 1.0f;
}

}
