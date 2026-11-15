#pragma once

#include "geometry/sphere.h"
#include "geometry/torus.h"

namespace hip_demo {

__device__ inline bool hit_described_primitive(const Ray& ray, const GpuPrimitive& object,
                                       float maximum_distance, Hit& hit) {
    return object.geometry.kind == GeometryKind::sphere
        ? object.geometry.sphere.intersect(ray, 0.001f, maximum_distance, hit)
        : object.geometry.torus.intersect(ray, 0.001f, maximum_distance, hit);
}

}
