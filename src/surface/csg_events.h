#pragma once

#include "surface/primitive.h"
#include "surface/text_solid.h"

namespace hip_demo {

__device__ inline Ray csg_local_ray(const Ray& ray, const ScenePrimitive& shape) {
    return {rotate_x(ray.origin - vector3(shape.center), -shape.tilt_sine, shape.tilt_cosine),
            rotate_x(ray.direction, -shape.tilt_sine, shape.tilt_cosine)};
}

__device__ inline bool csg_same_shape(const ScenePrimitive& a, const ScenePrimitive& b) {
    return a.sphere == b.sphere && a.center.x == b.center.x && a.center.y == b.center.y &&
        a.center.z == b.center.z && (a.sphere ? a.radius == b.radius :
        a.major_radius == b.major_radius && a.minor_radius == b.minor_radius &&
        a.tilt_sine == b.tilt_sine && a.tilt_cosine == b.tilt_cosine);
}

__device__ inline bool csg_same_shape(const SceneCsgLeaf& a, const SceneCsgLeaf& b) {
    if (a.text || b.text) return a.text && b.text && a.text_index == b.text_index;
    return csg_same_shape(a.shape, b.shape);
}

struct CsgEvent {
    float distance;
    float3 normal;
    std::uint32_t leaf, glyph_state;
    int positive, negative;
    bool cap, candidate, applied;
};

struct CsgGlyphState {
    std::uint32_t leaf, glyph;
    int positive, negative;
    bool z_inside, before;
};

struct CsgEventCache {
    // Bounded scratch storage, not a limit on supported CSG/text complexity.
    static constexpr unsigned int event_capacity = 64, glyph_capacity = 32, leaf_capacity = 64;
    static constexpr std::uint32_t primitive = csg_outside;
    CsgEvent events[event_capacity];
    CsgGlyphState glyphs[glyph_capacity];
    std::uint8_t aliases[leaf_capacity];
    unsigned int event_count = 0, glyph_count = 0;
    unsigned int leaf_count = 0;
    std::uint64_t primitive_inside = 0;

    __device__ bool add(CsgEvent event, float maximum, unsigned int merge_begin) {
        if (!(event.distance < maximum)) return true;
        for (unsigned int i = merge_begin; i < event_count; ++i) {
            auto& existing = events[i];
            if (existing.distance != event.distance || existing.glyph_state != event.glyph_state ||
                existing.leaf != event.leaf || existing.cap != event.cap) continue;
            existing.positive += event.positive;
            existing.negative += event.negative;
            if (!existing.candidate && event.candidate) {
                existing.normal = event.normal;
                existing.candidate = true;
            }
            return true;
        }
        if (event_count == event_capacity) return false;
        events[event_count++] = event;
        return true;
    }

    __device__ void advance(float boundary, bool inclusive) {
        for (unsigned int i = 0; i < event_count; ++i) {
            auto& event = events[i];
            if (event.applied || !(inclusive ? event.distance <= boundary : event.distance < boundary)) continue;
            event.applied = true;
            if (event.glyph_state == primitive) {
                primitive_inside ^= std::uint64_t{1} << event.leaf;
            } else {
                auto& glyph = glyphs[event.glyph_state];
                glyph.positive += event.positive;
                glyph.negative += event.negative;
                if (event.cap) glyph.z_inside = !glyph.z_inside;
            }
        }
    }

    __device__ bool glyph_inside(unsigned int i, TextGeometryView text, const Ray& ray, float boundary) const {
        const auto& state = glyphs[i];
        const auto& glyph = text.glyphs[state.glyph];
        return state.z_inside && text_detail::filled(glyph, state.positive) &&
            text_detail::filled(glyph, state.negative) &&
            text_solid_bounds(glyph, {ray.origin.x, ray.origin.y, ray.origin.z},
                              {ray.direction.x, ray.direction.y, ray.direction.z}, boundary, boundary);
    }

    __device__ std::uint64_t mask(TextGeometryView text, const Ray& ray, float boundary) const {
        auto result = primitive_inside;
        for (unsigned int i = 0; i < glyph_count; ++i)
            if (glyph_inside(i, text, ray, boundary)) result |= std::uint64_t{1} << glyphs[i].leaf;
        for (unsigned int i = 0; i < leaf_count; ++i)
            if ((result & (std::uint64_t{1} << aliases[i])) != 0) result |= std::uint64_t{1} << i;
        return result;
    }
};

__device__ inline bool csg_cache_glyph(CsgEventCache& cache, const Ray& ray, const TextGlyph& glyph,
                                      const TextCurve* curves, unsigned int leaf, unsigned int glyph_index,
                                      float minimum, float maximum) {
    const SceneVector origin{ray.origin.x, ray.origin.y, ray.origin.z};
    const SceneVector direction{ray.direction.x, ray.direction.y, ray.direction.z};
    if (!text_solid_bounds(glyph, origin, direction, minimum, maximum)) return true;
    const float ox = origin.x - glyph.position.x, oy = origin.y - glyph.position.y;
    const float oz = origin.z - glyph.position.z;
    if (direction.z == 0 && (oz <= 0 || oz >= glyph.depth)) return true;
    if (cache.glyph_count == CsgEventCache::glyph_capacity) return false;
    const auto state_index = cache.glyph_count++;
    const bool stationary = direction.x == 0 && direction.y == 0;
    const bool filled = stationary && text_detail::stationary_contains_xy(glyph, curves, ox, oy);
    cache.glyphs[state_index] = {leaf, glyph_index, filled ? 1 : 0, filled ? 1 : 0,
                                direction.z == 0, false};
    const auto caps_begin = cache.event_count;
    if (direction.z != 0) {
        for (int cap = 0; cap < 2; ++cap) {
            const float t = ((cap == 0 ? 0.0f : glyph.depth) - oz) / direction.z;
            const bool candidate = t >= minimum && t < maximum && filled;
            if (!cache.add({t, {0, 0, cap == 0 ? -1.0f : 1.0f}, leaf, state_index,
                            0, 0, true, candidate, false}, maximum, cache.event_count)) return false;
        }
    }
    if (stationary) return true;
    const auto curves_begin = cache.event_count;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto& curve = curves[glyph.first_curve + index];
        const auto crossings = text_detail::ray_curve_crossings(curve, ox, oy, direction.x, direction.y);
        const auto begin = cache.event_count;
        for (int root = 0; root < crossings.root_count; ++root) {
            const float u = crossings.roots[root];
            const float t = fabsf(direction.x) >= fabsf(direction.y)
                ? (text_detail::evaluate(curve.x, u) - ox) / direction.x
                : (text_detail::evaluate(curve.y, u) - oy) / direction.y;
            const float z = fmaf(t, direction.z, oz);
            if (t < minimum || !(t < maximum) || z < 0 || z > glyph.depth) continue;
            const float dx = text_detail::derivative(curve.x, u), dy = text_detail::derivative(curve.y, u);
            const float length = sqrtf(dx * dx + dy * dy);
            if (length == 0) continue;
            if (!cache.add({t, {glyph.normal_sign * dy / length, -glyph.normal_sign * dx / length, 0},
                            leaf, state_index, 0, 0, false, true, false}, maximum, begin)) return false;
        }
        for (int i = 0; i < crossings.count; ++i) {
            const auto& crossing = crossings.crossings[i];
            if (!cache.add({crossing.distance, {}, leaf, state_index, crossing.positive, crossing.negative,
                            false, false, false}, maximum, begin)) return false;
        }
    }
    for (unsigned int cap = caps_begin; cap < curves_begin; ++cap) {
        auto& event = cache.events[cap];
        const float t = event.distance;
        if (!(t >= minimum && t < maximum)) continue;
        int positive = 0, negative = 0;
        bool edge = false;
        const float tolerance = 16.0f * 0x1p-23f * fmaxf(1.0f, fabsf(t));
        for (unsigned int i = curves_begin; i < cache.event_count; ++i) {
            const auto& crossing = cache.events[i];
            if (crossing.distance < t) {
                positive += crossing.positive;
                negative += crossing.negative;
            }
            if (fabsf(crossing.distance - t) <= tolerance) edge = true;
        }
        // Only ambiguous cap/contour edges need the original point-fill tie-break.
        event.candidate = edge ?
            text_detail::contains(glyph, curves, ox + t * direction.x, oy + t * direction.y) :
            text_detail::filled(glyph, positive) && text_detail::filled(glyph, negative);
    }
    return true;
}

__device__ inline bool csg_cache_build(CsgEventCache& cache, const Ray& ray, const SceneCsg& object,
                                      const SceneCsgLeaf* leaves, TextGeometryView text,
                                      float minimum, float maximum) {
    if (object.leaf_count > CsgEventCache::leaf_capacity) return false;
    cache.leaf_count = object.leaf_count;
    for (std::uint32_t i = 0; i < object.leaf_count; ++i) {
        const auto& leaf = leaves[object.first_leaf + i];
        cache.aliases[i] = static_cast<std::uint8_t>(i);
        // Identical operands share roots but retain separate Boolean instructions.
        for (std::uint32_t previous = 0; previous < i; ++previous) {
            if (!csg_same_shape(leaf, leaves[object.first_leaf + previous])) continue;
            cache.aliases[i] = cache.aliases[previous];
            break;
        }
        if (cache.aliases[i] != i) continue;
        if (leaf.text) {
            for (std::uint32_t glyph = 0; glyph < leaf.glyph_count; ++glyph)
                if (!csg_cache_glyph(cache, ray, text.glyphs[leaf.first_glyph + glyph], text.curves,
                                     i, leaf.first_glyph + glyph, minimum, maximum)) return false;
        } else if (leaf.shape.sphere) {
            const auto center = vector3(leaf.shape.center);
            const auto roots = sphere_roots(ray, center, leaf.shape.radius);
            const auto offset = ray.origin - center;
            for (int root = 0; root < roots.count; ++root) {
                const float t = roots.values[root];
                const bool candidate = t >= minimum && t < maximum;
                const float3 normal = candidate ? normalize({fmaf(t, ray.direction.x, offset.x),
                    fmaf(t, ray.direction.y, offset.y), fmaf(t, ray.direction.z, offset.z)}) : float3{};
                // Do not merge primitive roots: even multiplicities cancel occupancy.
                if (!cache.add({t, normal, i, CsgEventCache::primitive, 0, 0, false, candidate, false},
                                maximum, cache.event_count)) return false;
            }
        } else {
            const auto& shape = leaf.shape;
            const auto torus = torus_roots(csg_local_ray(ray, shape), {}, shape.major_radius,
                                           shape.minor_radius, minimum, maximum);
            for (int root = 0; root < torus.roots.count; ++root) {
                const float t = torus.distance(root);
                const bool candidate = t >= minimum && t < maximum;
                const float3 normal = candidate ?
                    rotate_x(torus.normal(root), shape.tilt_sine, shape.tilt_cosine) : float3{};
                if (!cache.add({t, normal, i, CsgEventCache::primitive, 0, 0, false, candidate, false},
                                maximum, cache.event_count)) return false;
            }
        }
    }
    return true;
}

__device__ inline bool csg_cache_contains(const SceneCsg& object, const SceneCsgLeaf* leaves,
                                         std::uint64_t mask, const SceneCsgLeaf* override_leaf = nullptr,
                                         bool override_inside = false) {
    auto instruction = object.entry;
    while (instruction != csg_inside && instruction != csg_outside) {
        const auto& leaf = leaves[instruction];
        const bool inside = override_leaf && csg_same_shape(leaf, *override_leaf) ? override_inside :
            (mask & (std::uint64_t{1} << (instruction - object.first_leaf))) != 0;
        instruction = inside ? leaf.when_inside : leaf.when_outside;
    }
    return instruction == csg_inside;
}

__device__ __noinline__ inline bool intersect_csg_cached(const Ray& ray, const SceneCsg& object,
    const SceneCsgLeaf* leaves, float minimum, float maximum, Hit& hit, TextGeometryView text, bool& usable) {
    CsgEventCache cache;
    usable = csg_cache_build(cache, ray, object, leaves, text, minimum, maximum);
    if (!usable) return false;
    while (true) {
        float boundary = maximum;
        for (unsigned int i = 0; i < cache.event_count; ++i)
            if (cache.events[i].candidate) boundary = fminf(boundary, cache.events[i].distance);
        if (!(boundary < maximum)) return false;
        const float tolerance = 2.0f * 0x1p-23f * fmaxf(1.0f, fabsf(boundary));
        const float upper = fminf(boundary + tolerance, nextafterf(maximum, -INFINITY));
        cache.advance(boundary, false);
        const auto before_mask = cache.mask(text, ray, boundary);
        for (unsigned int i = 0; i < cache.glyph_count; ++i)
            cache.glyphs[i].before = cache.glyph_inside(i, text, ray, boundary);
        cache.advance(upper, true);
        const auto after_mask = cache.mask(text, ray, upper);
        const bool before = csg_cache_contains(object, leaves, before_mask);
        const bool after = csg_cache_contains(object, leaves, after_mask);
        if (before != after) {
            const CsgEvent* selected = nullptr;
            for (std::uint32_t leaf_index = 0; leaf_index < object.leaf_count; ++leaf_index) {
                const auto bit = std::uint64_t{1} << leaf_index;
                if (((before_mask ^ after_mask) & bit) == 0) continue;
                const auto& leaf = leaves[object.first_leaf + leaf_index];
                const CsgEvent* candidate = nullptr;
                for (unsigned int i = 0; i < cache.event_count; ++i) {
                    const auto& event = cache.events[i];
                    if (!event.candidate || event.leaf != leaf_index ||
                        event.distance < boundary || event.distance > upper) continue;
                    if (leaf.text && leaf.glyph_count > 1 &&
                        cache.glyphs[event.glyph_state].before ==
                        cache.glyph_inside(event.glyph_state, text, ray, upper)) continue;
                    if (!candidate || event.distance < candidate->distance) candidate = &event;
                }
                if (!candidate) continue;
                if (!selected) selected = candidate;
                const bool leaf_before = (before_mask & bit) != 0;
                if (csg_cache_contains(object, leaves, before_mask, &leaf, !leaf_before) != before ||
                    csg_cache_contains(object, leaves, after_mask, &leaf, leaf_before) != after) {
                    selected = candidate;
                    break;
                }
            }
            if (selected) {
                auto normal = selected->normal;
                if ((dot(normal, ray.direction) < 0) != after) normal = -normal;
                hit.distance = selected->distance;
                hit.point = ray.origin + hit.distance * ray.direction;
                set_face_normal(hit, ray, normal);
                return true;
            }
        }
        for (unsigned int i = 0; i < cache.event_count; ++i)
            if (cache.events[i].distance <= upper) cache.events[i].candidate = false;
    }
}

}
