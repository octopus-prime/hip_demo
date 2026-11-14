#pragma once

#include "surface/primitive.h"

namespace hip_demo {

__device__ inline Ray csg_local_ray(const Ray& ray, const ScenePrimitive& shape) {
    return {rotate_x(ray.origin - vector3(shape.center), -shape.tilt_sine, shape.tilt_cosine),
            rotate_x(ray.direction, -shape.tilt_sine, shape.tilt_cosine)};
}

__device__ inline PolynomialRoots csg_leaf_roots(const Ray& ray, const ScenePrimitive& shape,
                                                float minimum, float maximum) {
    if (shape.sphere) return sphere_roots(ray, vector3(shape.center), shape.radius);
    const auto torus = torus_roots(csg_local_ray(ray, shape), {}, shape.major_radius,
                                   shape.minor_radius, minimum, maximum);
    PolynomialRoots roots;
    for (int i = 0; i < torus.roots.count; ++i) roots.add(torus.distance(i));
    return roots;
}

__device__ inline bool csg_same_shape(const ScenePrimitive& a, const ScenePrimitive& b) {
    return a.sphere == b.sphere && a.center.x == b.center.x && a.center.y == b.center.y &&
        a.center.z == b.center.z && (a.sphere ? a.radius == b.radius :
        a.major_radius == b.major_radius && a.minor_radius == b.minor_radius &&
        a.tilt_sine == b.tilt_sine && a.tilt_cosine == b.tilt_cosine);
}

__device__ inline bool csg_contains(const Ray& ray, const SceneCsg& object, const SceneCsgLeaf* leaves,
                                    float boundary, bool inclusive, float minimum, float maximum,
                                    const ScenePrimitive* override_shape = nullptr, bool override_inside = false) {
    auto instruction = object.entry;
    while (instruction != csg_inside && instruction != csg_outside) {
        const auto& leaf = leaves[instruction];
        bool inside = override_inside;
        if (!override_shape || !csg_same_shape(leaf.shape, *override_shape)) {
            const auto roots = csg_leaf_roots(ray, leaf.shape, minimum, maximum);
            inside = false;
            for (int i = 0; i < roots.count; ++i) {
                if (inclusive ? roots.values[i] <= boundary : roots.values[i] < boundary) inside = !inside;
            }
        }
        instruction = inside ? leaf.when_inside : leaf.when_outside;
    }
    return instruction == csg_inside;
}

__device__ inline float3 csg_leaf_normal(const Ray& ray, const ScenePrimitive& shape, float distance,
                                        float minimum, float maximum) {
    if (shape.sphere) {
        const float3 offset = ray.origin - vector3(shape.center);
        return normalize({fmaf(distance, ray.direction.x, offset.x),
                          fmaf(distance, ray.direction.y, offset.y),
                          fmaf(distance, ray.direction.z, offset.z)});
    }
    const auto torus = torus_roots(csg_local_ray(ray, shape), {}, shape.major_radius,
                                   shape.minor_radius, minimum, maximum);
    int closest = 0;
    for (int i = 1; i < torus.roots.count; ++i)
        if (fabsf(torus.distance(i) - distance) < fabsf(torus.distance(closest) - distance)) closest = i;
    return rotate_x(torus.normal(closest), shape.tilt_sine, shape.tilt_cosine);
}

__device__ inline bool intersect_csg(const Ray& ray, const SceneCsg& object, const SceneCsgLeaf* leaves,
                                    float minimum, float maximum, Hit& hit) {
    float cursor = minimum;
    bool first = true;
    while (cursor < maximum) {
        float boundary = maximum;
        for (std::uint32_t i = 0; i < object.leaf_count; ++i) {
            const auto roots = csg_leaf_roots(ray, leaves[object.first_leaf + i].shape, minimum, maximum);
            for (int root = 0; root < roots.count; ++root) {
                const float t = roots.values[root];
                if ((first ? t >= cursor : t > cursor) && t < boundary) boundary = t;
            }
        }
        if (boundary >= maximum) return false;
        // Group coincident FP32 boundaries; repeated roots must not toggle occupancy.
        const float tolerance = 2.0f * 0x1p-23f * fmaxf(1.0f, fabsf(boundary));
        const float upper = fminf(boundary + tolerance, nextafterf(maximum, -INFINITY));
        const bool before = csg_contains(ray, object, leaves, boundary, false, minimum, maximum);
        const bool after = csg_contains(ray, object, leaves, upper, true, minimum, maximum);
        if (before != after) {
            std::uint32_t selected = csg_outside;
            float selected_distance = boundary;
            for (std::uint32_t i = 0; i < object.leaf_count; ++i) {
                const auto& shape = leaves[object.first_leaf + i].shape;
                const auto roots = csg_leaf_roots(ray, shape, minimum, maximum);
                int crossings = 0;
                bool leaf_before = false;
                float distance = boundary;
                for (int root = 0; root < roots.count; ++root) {
                    if (roots.values[root] < boundary) leaf_before = !leaf_before;
                    if (roots.values[root] >= boundary && roots.values[root] <= upper) {
                        if (crossings == 0) distance = roots.values[root];
                        ++crossings;
                    }
                }
                if (crossings % 2 == 0) continue;
                if (selected == csg_outside) {
                    selected = i;
                    selected_distance = distance;
                }
                // Ignore hidden/cancelled operands when coincident boundaries meet.
                // Equal shapes must change together, including repeated A - A operands.
                if (csg_contains(ray, object, leaves, boundary, false, minimum, maximum,
                                 &shape, !leaf_before) != before ||
                    csg_contains(ray, object, leaves, upper, true, minimum, maximum,
                                 &shape, leaf_before) != after) {
                    selected = i;
                    selected_distance = distance;
                    break;
                }
            }
            if (selected != csg_outside) {
                const auto& shape = leaves[object.first_leaf + selected].shape;
                float3 normal = csg_leaf_normal(ray, shape, selected_distance, minimum, maximum);
                if ((dot(normal, ray.direction) < 0.0f) != after) normal = -normal;
                hit.distance = selected_distance;
                hit.point = ray.origin + selected_distance * ray.direction;
                set_face_normal(hit, ray, normal);
                return true;
            }
        }
        cursor = upper;
        first = false;
    }
    return false;
}

}
