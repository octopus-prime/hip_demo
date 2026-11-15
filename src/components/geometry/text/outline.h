#pragma once

#include "scene/types.h"
#include "gpu/types.h"
#include "math/polynomial.h"

#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__HIPCC__)
#define TEXT_HD __host__ __device__
#else
#define TEXT_HD
#endif

struct TextCurve {
    float x[4]{}, y[4]{};
};

struct TextGlyph {
    SceneVector position, minimum, maximum;
    GpuMaterial material;
    float depth = 0, normal_sign = 1;
    std::uint32_t first_curve = 0, curve_count = 0, object_id = 0;
    bool even_odd = false;
};

struct TextGeometry {
    std::vector<TextCurve> curves;
    std::vector<TextGlyph> glyphs;
};

struct TextGeometryView {
    const TextGlyph* glyphs = nullptr;
    const TextCurve* curves = nullptr;
};

void append_text_geometry(TextGeometry& geometry, const hip_demo::ExtrudedText& text,
                          std::uint32_t object_id, GpuMaterial material = {});

namespace text_detail {
TEXT_HD inline float evaluate(const float* p, float u) {
    return fmaf(fmaf(fmaf(p[3], u, p[2]), u, p[1]), u, p[0]);
}

TEXT_HD inline float derivative(const float* p, float u) {
    return fmaf(fmaf(3.0f * p[3], u, 2.0f * p[2]), u, p[1]);
}

TEXT_HD inline void add_root(float* roots, int& count, float u) {
    constexpr float tolerance = 8.0f * 0x1p-23f;
    if (u < -tolerance || u > 1.0f + tolerance) return;
    u = fmaxf(0.0f, fminf(1.0f, u));
    int index = 0;
    while (index < count && roots[index] < u) ++index;
    if ((index < count && fabsf(roots[index] - u) <= tolerance) ||
        (index > 0 && fabsf(roots[index - 1] - u) <= tolerance)) return;
    for (int i = count; i > index; --i) roots[i] = roots[i - 1];
    roots[index] = u;
    ++count;
}

TEXT_HD inline int quadratic_roots(float a, float b, float c, float* roots) {
    const auto solutions = hip_demo::quadratic_roots(a, b, c);
    int count = 0;
    for (int i = 0; i < solutions.count; ++i) add_root(roots, count, solutions.values[i]);
    return count;
}

TEXT_HD inline int curve_roots(const float* coefficients, float* roots) {
    const auto solutions = hip_demo::cubic_roots(
        coefficients[3], coefficients[2], coefficients[1], coefficients[0]);
    int count = 0;
    for (int i = 0; i < solutions.count; ++i) add_root(roots, count, solutions.values[i]);
    return count;
}

TEXT_HD inline bool contains(const TextGlyph& glyph, const TextCurve* curves, float x, float y) {
    int winding = 0;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto& curve = curves[glyph.first_curve + index];
        float cuts[4]{0, 0, 0, 1};
        const int critical = quadratic_roots(3 * curve.y[3], 2 * curve.y[2], curve.y[1], cuts + 1);
        cuts[critical + 1] = 1;
        const float polynomial[4]{curve.y[0] - y, curve.y[1], curve.y[2], curve.y[3]};
        float roots[4];
        const int count = curve_roots(polynomial, roots);
        for (int part = 0; part <= critical; ++part) {
            const float low = cuts[part], high = cuts[part + 1];
            const float start = evaluate(curve.y, low), end = evaluate(curve.y, high);
            const bool upward = end > start;
            // Half-open intervals count shared vertices once; extrema do not toggle the fill.
            if (!(upward ? start <= y && y < end : end <= y && y < start)) continue;
            for (int root = 0; root < count; ++root) {
                constexpr float tolerance = 8.0f * 0x1p-23f;
                if (roots[root] < low - tolerance || roots[root] > high + tolerance) continue;
                const float u = fmaxf(low, fminf(high, roots[root]));
                if (evaluate(curve.x, u) > x) winding += upward ? 1 : -1;
                break;
            }
        }
    }
    return glyph.even_odd ? winding % 2 != 0 : winding != 0;
}
}

struct TextIntersection {
    float distance;
    SceneVector normal;
};

TEXT_HD inline bool intersect_text(const TextGlyph& glyph, const TextCurve* curves,
                                   SceneVector origin, SceneVector direction,
                                   float minimum, float maximum, TextIntersection& hit) {
    const float ox = origin.x - glyph.position.x;
    const float oy = origin.y - glyph.position.y;
    const float oz = origin.z - glyph.position.z;
    float closest = maximum;
    bool found = false;
    if (direction.z != 0) {
        for (int cap = 0; cap < 2; ++cap) {
            const float t = ((cap == 0 ? 0.0f : glyph.depth) - oz) / direction.z;
            if (t >= minimum && t < closest &&
                text_detail::contains(glyph, curves, ox + t * direction.x, oy + t * direction.y)) {
                closest = t;
                hit.normal = {0, 0, cap == 0 ? -1.0f : 1.0f};
                found = true;
            }
        }
    }
    if (direction.x != 0 || direction.y != 0) {
        for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
            const auto& curve = curves[glyph.first_curve + index];
            float p[4];
            for (int i = 0; i < 4; ++i) p[i] = fmaf(curve.x[i], direction.y, -curve.y[i] * direction.x);
            p[0] += fmaf(oy, direction.x, -ox * direction.y);
            float roots[4];
            const int count = text_detail::curve_roots(p, roots);
            for (int root = 0; root < count; ++root) {
                const float u = roots[root];
                const float t = fabsf(direction.x) >= fabsf(direction.y)
                    ? (text_detail::evaluate(curve.x, u) - ox) / direction.x
                    : (text_detail::evaluate(curve.y, u) - oy) / direction.y;
                const float z = fmaf(t, direction.z, oz);
                if (t < minimum || t >= closest || z < 0 || z > glyph.depth) continue;
                const float dx = text_detail::derivative(curve.x, u);
                const float dy = text_detail::derivative(curve.y, u);
                const float length = sqrtf(dx * dx + dy * dy);
                if (length == 0) continue;
                closest = t;
                hit.normal = {glyph.normal_sign * dy / length, -glyph.normal_sign * dx / length, 0};
                found = true;
            }
        }
    }
    if (found) hit.distance = closest;
    return found;
}

#undef TEXT_HD
