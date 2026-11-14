#pragma once

#include "surface/hit.h"

namespace hip_demo {

constexpr float floor_tile_size = 0.75f;
constexpr float floor_grout_half_width = 0.009f;
constexpr float floor_bevel_width = 0.022f;
constexpr float floor_grout_depth = 0.012f;

__device__ inline float tile_edge(float coordinate, float& slope) {
    const float scaled = coordinate / floor_tile_size;
    const float fraction = scaled - floorf(scaled);
    const float distance = fminf(fraction, 1.0f - fraction) * floor_tile_size;
    const float t = clamp01((distance - floor_grout_half_width) / floor_bevel_width);
    slope = (fraction < 0.5f ? 1.0f : -1.0f) * 6.0f * t * (1.0f - t) / floor_bevel_width;
    return t * t * (3.0f - 2.0f * t);
}

__device__ inline void apply_floor_tiles(Hit& hit, const Ray& ray) {
    float slope_x = 0.0f, slope_z = 0.0f;
    const float edge_x = tile_edge(hit.point.x, slope_x);
    const float edge_z = tile_edge(hit.point.z, slope_z);
    const float tile = edge_x * edge_z;
    const int x = static_cast<int>(floorf(hit.point.x / floor_tile_size));
    const int z = static_cast<int>(floorf(hit.point.z / floor_tile_size));
    std::uint32_t hash = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
                         static_cast<std::uint32_t>(z) * 0xd8163841u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    const float variation = 0.94f + 0.10f * static_cast<float>(hash & 255u) / 255.0f;
    const float3 tile_color = variation * float3{0.73f, 0.72f, 0.69f};
    const float3 grout_color{0.30f, 0.29f, 0.28f};
    hit.albedo = grout_color * (1.0f - tile) + tile_color * tile;
    const float3 outward = normalize({-floor_grout_depth * slope_x * edge_z, 1.0f,
                                      -floor_grout_depth * slope_z * edge_x});
    const float3 shading_normal = hit.front_face ? outward : -outward;
    if (dot(ray.direction, shading_normal) < 0.0f) {
        hit.normal = shading_normal;
    }
    hit.tiled_floor = true;
}

}
