#pragma once

#include "components/geometry/sphere/intersection.h"
#include "components/geometry/quadric/intersection.h"
#include "components/geometry/torus/intersection.h"
#include "components/geometry/box/intersection.h"

namespace hip_demo {

__device__ inline bool hit_described_primitive(const Ray& ray, const GpuPrimitive& object,
                                       float minimum_distance, float maximum_distance, Hit& hit) {
    if (object.geometry.kind == GeometryKind::quadric)
        return object.geometry.quadric.intersect(ray, minimum_distance, maximum_distance, hit);
    if (object.geometry.kind == GeometryKind::box)
        return object.geometry.box.intersect(ray, minimum_distance, maximum_distance, hit);
    return object.geometry.torus.intersect(ray, minimum_distance, maximum_distance, hit);
}

}
