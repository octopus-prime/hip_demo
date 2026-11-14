#pragma once

#include "surface/primitive.h"
#include "surface/text_solid.h"

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

__device__ inline bool csg_leaf_contains(const Ray& ray, const SceneCsgLeaf& leaf, TextGeometryView text,
                                         float boundary, bool inclusive, float minimum, float maximum) {
    if (leaf.text) {
        const SceneVector origin{ray.origin.x, ray.origin.y, ray.origin.z};
        const SceneVector direction{ray.direction.x, ray.direction.y, ray.direction.z};
        for (std::uint32_t i = 0; i < leaf.glyph_count; ++i) {
            const auto& glyph = text.glyphs[leaf.first_glyph + i];
            if (text_solid_bounds(glyph, origin, direction, boundary, boundary) &&
                text_solid_contains(glyph, text.curves, origin, direction, boundary, inclusive)) return true;
        }
        return false;
    }
    const auto roots = csg_leaf_roots(ray, leaf.shape, minimum, maximum);
    bool inside = false;
    for (int i = 0; i < roots.count; ++i)
        if (inclusive ? roots.values[i] <= boundary : roots.values[i] < boundary) inside = !inside;
    return inside;
}

__device__ inline bool csg_leaf_boundary(const Ray& ray, const SceneCsgLeaf& leaf, TextGeometryView text,
                                         float minimum, float maximum, TextIntersection& hit,
                                         bool crossing_only = false) {
    if (leaf.text) {
        const SceneVector origin{ray.origin.x, ray.origin.y, ray.origin.z};
        const SceneVector direction{ray.direction.x, ray.direction.y, ray.direction.z};
        const float upper = nextafterf(maximum, -INFINITY);
        bool found = false;
        for (std::uint32_t i = 0; i < leaf.glyph_count; ++i) {
            const auto& glyph = text.glyphs[leaf.first_glyph + i];
            if (!text_solid_bounds(glyph, origin, direction, minimum, maximum)) continue;
            // A neighbouring tangent/internal glyph must not supply the union's normal.
            if (crossing_only && leaf.glyph_count > 1 &&
                text_solid_contains(glyph, text.curves, origin, direction, minimum, false) ==
                text_solid_contains(glyph, text.curves, origin, direction, upper, true)) continue;
            if (intersect_text(glyph, text.curves, origin, direction, minimum, maximum, hit)) {
                maximum = hit.distance;
                found = true;
            }
        }
        return found;
    }
    const auto roots = csg_leaf_roots(ray, leaf.shape, minimum, maximum);
    for (int i = 0; i < roots.count; ++i) {
        const float distance = roots.values[i];
        if (distance < minimum || distance >= maximum) continue;
        hit.distance = distance;
        const auto normal = csg_leaf_normal(ray, leaf.shape, distance, minimum, maximum);
        hit.normal = {normal.x, normal.y, normal.z};
        return true;
    }
    return false;
}

__device__ inline bool csg_same_shape(const SceneCsgLeaf& a, const SceneCsgLeaf& b) {
    if (a.text || b.text) return a.text && b.text && a.text_index == b.text_index;
    return csg_same_shape(a.shape, b.shape);
}

__device__ inline bool csg_contains(const Ray& ray, const SceneCsg& object, const SceneCsgLeaf* leaves,
                                    TextGeometryView text, float boundary, bool inclusive,
                                    float minimum, float maximum,
                                    const SceneCsgLeaf* override_leaf = nullptr, bool override_inside = false) {
    auto instruction = object.entry;
    while (instruction != csg_inside && instruction != csg_outside) {
        const auto& leaf = leaves[instruction];
        const bool inside = override_leaf && csg_same_shape(leaf, *override_leaf) ? override_inside :
            csg_leaf_contains(ray, leaf, text, boundary, inclusive, minimum, maximum);
        instruction = inside ? leaf.when_inside : leaf.when_outside;
    }
    return instruction == csg_inside;
}

__device__ inline bool intersect_csg(const Ray& ray, const SceneCsg& object, const SceneCsgLeaf* leaves,
                                    float minimum, float maximum, Hit& hit, TextGeometryView text = {}) {
    float cursor = minimum;
    bool first = true;
    while (cursor < maximum) {
        float boundary = maximum;
        for (std::uint32_t i = 0; i < object.leaf_count; ++i) {
            TextIntersection candidate{};
            if (csg_leaf_boundary(ray, leaves[object.first_leaf + i], text,
                                  first ? cursor : nextafterf(cursor, INFINITY), boundary, candidate))
                boundary = candidate.distance;
        }
        if (boundary >= maximum) return false;
        // Group coincident FP32 boundaries; repeated roots must not toggle occupancy.
        const float tolerance = 2.0f * 0x1p-23f * fmaxf(1.0f, fabsf(boundary));
        const float upper = fminf(boundary + tolerance, nextafterf(maximum, -INFINITY));
        const bool before = csg_contains(ray, object, leaves, text, boundary, false, minimum, maximum);
        const bool after = csg_contains(ray, object, leaves, text, upper, true, minimum, maximum);
        if (before != after) {
            std::uint32_t selected = csg_outside;
            TextIntersection selected_hit{};
            for (std::uint32_t i = 0; i < object.leaf_count; ++i) {
                const auto& leaf = leaves[object.first_leaf + i];
                const bool leaf_before = csg_leaf_contains(ray, leaf, text, boundary, false, minimum, maximum);
                if (leaf_before == csg_leaf_contains(ray, leaf, text, upper, true, minimum, maximum)) continue;
                TextIntersection candidate{};
                if (!csg_leaf_boundary(ray, leaf, text, boundary, nextafterf(upper, INFINITY), candidate, true)) continue;
                if (selected == csg_outside) {
                    selected = i;
                    selected_hit = candidate;
                }
                // Ignore hidden/cancelled operands when coincident boundaries meet.
                // Equal shapes must change together, including repeated A - A operands.
                if (csg_contains(ray, object, leaves, text, boundary, false, minimum, maximum,
                                 &leaf, !leaf_before) != before ||
                    csg_contains(ray, object, leaves, text, upper, true, minimum, maximum,
                                 &leaf, leaf_before) != after) {
                    selected = i;
                    selected_hit = candidate;
                    break;
                }
            }
            if (selected != csg_outside) {
                float3 normal = vector3(selected_hit.normal);
                if ((dot(normal, ray.direction) < 0.0f) != after) normal = -normal;
                hit.distance = selected_hit.distance;
                hit.point = ray.origin + selected_hit.distance * ray.direction;
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
