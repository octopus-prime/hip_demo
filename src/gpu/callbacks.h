#pragma once

#include "gpu/scene_view.h"
#include "surface/primitive.h"
#include "surface/csg.h"

// The linked HIP RT device library calls these global dispatch functions.
__device__ bool intersectFunc(std::uint32_t geom_type, std::uint32_t ray_type, const hiprtFuncTableHeader& table,
                              const hiprtRay& ray, void*, hiprtHit& result) {
    const auto* data = static_cast<const hip_demo::PrimitiveData*>(
        table.funcDataSets[geom_type * table.numRayTypes + ray_type].intersectFuncData);
    const hip_demo::Ray scene_ray{ray.origin, ray.direction};
    if (result.primID >= data->count && result.primID - data->count < data->csg_count) {
        hip_demo::Hit hit{};
        if (!hip_demo::intersect_csg(scene_ray, data->csgs[result.primID - data->count], data->csg_leaves,
                                     ray.minT, ray.maxT, hit)) return false;
        result.t = hit.distance;
        result.normal = hit.front_face ? hit.normal : -hit.normal;
        return true;
    }
    if (result.primID >= data->count) {
        const auto index = result.primID - data->count - data->csg_count;
        TextIntersection hit{};
        if (index >= data->glyph_count ||
            !intersect_text(data->glyphs[index], data->curves,
                            {ray.origin.x, ray.origin.y, ray.origin.z},
                            {ray.direction.x, ray.direction.y, ray.direction.z},
                            ray.minT, ray.maxT, hit)) return false;
        result.t = hit.distance;
        result.normal = {hit.normal.x, hit.normal.y, hit.normal.z};
        return true;
    }
    hip_demo::Hit hit{};
    if (result.primID >= data->count ||
        !hip_demo::hit_described_primitive(scene_ray, data->objects[result.primID], ray.maxT, hit) ||
        hit.distance < ray.minT) {
        return false;
    }
    result.t = hit.distance;
    result.normal = hit.front_face ? hit.normal : -hit.normal;
    return true;
}

__device__ bool filterFunc(std::uint32_t, std::uint32_t, const hiprtFuncTableHeader&,
                           const hiprtRay&, void*, const hiprtHit&) {
    return false;
}
