#pragma once

#include "components/geometry/quadric/model.h"
#include "geometry/hit.h"
#include "math/polynomial.h"

namespace hip_demo {
struct QuadricRoots {
    PolynomialRoots roots;
    float middle = 0, time_scale = 1;
    __device__ float distance(int index) const {
        return fmaf(roots.values[index], time_scale, middle);
    }
};

__device__ inline float GpuQuadric::evaluate(float3 p) const {
    return fmaf(diagonal.x, p.x * p.x, fmaf(diagonal.y, p.y * p.y, fmaf(diagonal.z, p.z * p.z,
        fmaf(cross.x, p.x * p.y, fmaf(cross.y, p.x * p.z, fmaf(cross.z, p.y * p.z,
        fmaf(linear.x, p.x, fmaf(linear.y, p.y, fmaf(linear.z, p.z, constant)))))))));
}

__device__ inline float3 GpuQuadric::gradient(float3 p) const {
    return {fmaf(2 * diagonal.x, p.x, fmaf(cross.x, p.y, fmaf(cross.y, p.z, linear.x))),
            fmaf(2 * diagonal.y, p.y, fmaf(cross.x, p.x, fmaf(cross.z, p.z, linear.y))),
            fmaf(2 * diagonal.z, p.z, fmaf(cross.y, p.x, fmaf(cross.z, p.y, linear.z)))};
}

__device__ inline QuadricRoots GpuQuadric::intersections(const Ray& ray) const {
    const float3 offset = ray.origin - vector3(center), extent = vector3(scale);
    float low = -INFINITY, high = INFINITY;
    const float origins[]{offset.x, offset.y, offset.z};
    const float directions[]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float extents[]{extent.x, extent.y, extent.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (directions[axis] == 0) {
            if (fabsf(origins[axis]) > extents[axis]) return {};
            continue;
        }
        const float a = (-extents[axis] - origins[axis]) / directions[axis];
        const float b = (extents[axis] - origins[axis]) / directions[axis];
        low = fmaxf(low, fminf(a, b));
        high = fminf(high, fmaxf(a, b));
        if (low > high) return {};
    }
    if (!isfinite(low) || !isfinite(high)) return {};
    const float3 velocity = ray.direction / extent;
    const float speed = fmaxf(fabsf(velocity.x), fmaxf(fabsf(velocity.y), fabsf(velocity.z)));
    if (!(speed > 0) || !isfinite(speed)) return {};
    QuadricRoots result;
    // Recenter within the finite bounds and normalize ray time before expanding the polynomial.
    result.middle = low * 0.5f + high * 0.5f;
    result.time_scale = 1 / speed;
    const float3 o{fmaf(result.middle, ray.direction.x, offset.x) / extent.x,
                   fmaf(result.middle, ray.direction.y, offset.y) / extent.y,
                   fmaf(result.middle, ray.direction.z, offset.z) / extent.z};
    const float3 d = velocity / speed;
    const float a = diagonal.x * d.x * d.x + diagonal.y * d.y * d.y + diagonal.z * d.z * d.z +
                    cross.x * d.x * d.y + cross.y * d.x * d.z + cross.z * d.y * d.z;
    result.roots = quadratic_roots(a, dot(gradient(o), d), evaluate(o));
    return result;
}

__device__ inline float3 GpuQuadric::normal_at(const Ray& ray, float distance) const {
    const auto offset = ray.origin - vector3(center);
    const float3 p{fmaf(distance, ray.direction.x, offset.x) / scale.x,
                   fmaf(distance, ray.direction.y, offset.y) / scale.y,
                   fmaf(distance, ray.direction.z, offset.z) / scale.z};
    const auto n = gradient(p) / vector3(scale);
    const float magnitude = fmaxf(fabsf(n.x), fmaxf(fabsf(n.y), fabsf(n.z)));
    return magnitude > 0 ? normalize(n / magnitude) : float3{};
}

__device__ inline bool GpuQuadric::intersect(const Ray& ray, float minimum, float maximum, Hit& hit) const {
    bool found = false;
    const auto offset = ray.origin - vector3(center);
    if (capped && ray.direction.y != 0) {
        for (const float y : {-1.0f, 1.0f}) {
            const float t = (y * scale.y - offset.y) / ray.direction.y;
            if (t < minimum || t >= maximum) continue;
            const float3 p{fmaf(t, ray.direction.x, offset.x) / scale.x, y,
                           fmaf(t, ray.direction.z, offset.z) / scale.z};
            const float axis_value = evaluate({0, y, 0});
            // A zero-area cap must not turn a cone's apex into a small disk.
            const float tolerance = axis_value == 0 ? 0 : 4 * 0x1p-23f;
            if (evaluate(p) > tolerance) continue;
            hit.distance = maximum = t;
            hit.point = ray.origin + t * ray.direction;
            set_face_normal(hit, ray, {0, y, 0});
            found = true;
        }
    }
    const auto roots = intersections(ray);
    for (int i = 0; i < roots.roots.count; ++i) {
        const float t = roots.distance(i);
        if (t < minimum || t >= maximum) continue;
        const float y = fmaf(t, ray.direction.y, offset.y) / scale.y;
        if (capped && fabsf(y) > 1 + 4 * 0x1p-23f) continue;
        if (found && maximum - t <= 2 * 0x1p-23f * fmaxf(1, fabsf(t))) continue;
        const auto normal = normal_at(ray, t);
        if (dot(normal, normal) == 0) continue;
        hit.distance = maximum = t;
        hit.point = ray.origin + t * ray.direction;
        set_face_normal(hit, ray, normal);
        found = true;
    }
    return found;
}

__device__ inline bool GpuQuadric::same_shape(const GpuQuadric& other) const {
    const auto equal = [] __device__ (SceneVector a, SceneVector b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    return equal(center, other.center) && equal(scale, other.scale) && equal(diagonal, other.diagonal) &&
           equal(cross, other.cross) && equal(linear, other.linear) &&
           constant == other.constant && capped == other.capped;
}
}
