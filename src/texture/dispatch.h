#pragma once

#include "texture/tiles.h"
#include "texture/wood.h"
#include "texture/marble.h"
#include "texture/mandelbrot.h"
#include "texture/julia.h"
#include "scene/defaults.h"

namespace hip_demo {

__device__ inline void apply_texture(Hit& hit, const Ray& ray, const GpuSceneSettings* settings,
                                     int wall = -1) {
    const auto point = hit.point;
    if (hit.texture == TextureKind::tiles) {
        apply_floor_tiles(hit, ray);
    } else if (hit.texture == TextureKind::wood) {
        hit.albedo = wood_albedo(wall == 1 ? float3{point.x, point.z, 0} : point);
    } else if (hit.texture == TextureKind::marble) {
        hit.albedo = marble_albedo(point);
    } else if (hit.texture == TextureKind::mandelbrot || hit.texture == TextureKind::julia) {
        const float depth = settings ? settings->room_size.z : 2 * room_half_depth;
        const float height = settings ? settings->room_size.y : room_height;
        hit.albedo = hit.texture == TextureKind::julia
            ? julia_albedo(point, depth, height) : mandelbrot_albedo(point, depth, height);
    }
}

}
