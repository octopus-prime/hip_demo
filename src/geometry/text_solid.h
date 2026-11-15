#pragma once

#include "geometry/text.h"

#if defined(__HIPCC__)
#define TEXT_SOLID_HD __host__ __device__
#else
#define TEXT_SOLID_HD
#endif

namespace text_detail {

TEXT_SOLID_HD inline bool filled(const TextGlyph& glyph, int winding) {
    return glyph.even_odd ? winding % 2 != 0 : winding != 0;
}

struct RayCurveCrossings {
    struct Crossing { float distance; int positive, negative; };
    float roots[4];
    int root_count;
    Crossing crossings[3];
    int count;
};

TEXT_SOLID_HD inline RayCurveCrossings ray_curve_crossings(const TextCurve& curve,
                                                           float ox, float oy, float dx, float dy) {
    RayCurveCrossings result{};
    float p[4];
    for (int i = 0; i < 4; ++i) p[i] = fmaf(curve.x[i], dy, -curve.y[i] * dx);
    p[0] += fmaf(oy, dx, -ox * dy);
    float cuts[4]{0, 0, 0, 1};
    const int critical = quadratic_roots(3 * p[3], 2 * p[2], p[1], cuts + 1);
    cuts[critical + 1] = 1;
    result.root_count = curve_roots(p, result.roots);
    for (int part = 0; part <= critical; ++part) {
        const float low = cuts[part], high = cuts[part + 1];
        const float start = evaluate(p, low), end = evaluate(p, high);
        const bool upward = end > start;
        // Both transverse limits must be filled: travelling along a contour
        // is not a solid interval. The paired half-open rules also cancel tangencies.
        const bool plus = upward ? start <= 0 && 0 < end : end <= 0 && 0 < start;
        const bool minus = upward ? start < 0 && 0 <= end : end < 0 && 0 <= start;
        if (!plus && !minus) continue;
        for (int root = 0; root < result.root_count; ++root) {
            constexpr float tolerance = 8.0f * 0x1p-23f;
            if (result.roots[root] < low - tolerance || result.roots[root] > high + tolerance) continue;
            const float u = fmaxf(low, fminf(high, result.roots[root]));
            const float t = fabsf(dx) >= fabsf(dy)
                ? (evaluate(curve.x, u) - ox) / dx : (evaluate(curve.y, u) - oy) / dy;
            const int sign = upward ? 1 : -1;
            result.crossings[result.count++] = {t, plus ? sign : 0, minus ? sign : 0};
            break;
        }
    }
    return result;
}

TEXT_SOLID_HD inline bool ray_contains_xy(const TextGlyph& glyph, const TextCurve* curves,
                                          float ox, float oy, float dx, float dy,
                                          float boundary, bool inclusive) {
    int positive = 0, negative = 0;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto crossings = ray_curve_crossings(curves[glyph.first_curve + index], ox, oy, dx, dy);
        for (int i = 0; i < crossings.count; ++i) {
            const auto& crossing = crossings.crossings[i];
            if (inclusive ? crossing.distance <= boundary : crossing.distance < boundary) {
                positive += crossing.positive;
                negative += crossing.negative;
            }
        }
    }
    return filled(glyph, positive) && filled(glyph, negative);
}

TEXT_SOLID_HD inline bool stationary_contains_xy(const TextGlyph& glyph, const TextCurve* curves,
                                                 float ox, float oy) {
    int before_positive = 0, before_negative = 0, after_positive = 0, after_negative = 0;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto crossings = ray_curve_crossings(curves[glyph.first_curve + index], ox, oy, 1, 0);
        for (int i = 0; i < crossings.count; ++i) {
            const auto& crossing = crossings.crossings[i];
            if (crossing.distance < 0) {
                before_positive += crossing.positive;
                before_negative += crossing.negative;
            }
            if (crossing.distance <= 0) {
                after_positive += crossing.positive;
                after_negative += crossing.negative;
            }
        }
    }
    return filled(glyph, before_positive) && filled(glyph, before_negative) &&
        filled(glyph, after_positive) && filled(glyph, after_negative);
}

TEXT_SOLID_HD inline bool clip_axis(float origin, float direction, float low, float high,
                                    float& minimum, float& maximum) {
    if (direction == 0) return origin >= low && origin <= high;
    const float a = (low - origin) / direction, b = (high - origin) / direction;
    minimum = fmaxf(minimum, fminf(a, b));
    maximum = fminf(maximum, fmaxf(a, b));
    return minimum <= maximum;
}

}

TEXT_SOLID_HD inline bool text_solid_bounds(const TextGlyph& glyph, SceneVector origin,
                                           SceneVector direction, float minimum, float maximum) {
    constexpr float margin = 0.01f;
    return text_detail::clip_axis(origin.x, direction.x, glyph.minimum.x - margin, glyph.maximum.x + margin,
                                 minimum, maximum) &&
        text_detail::clip_axis(origin.y, direction.y, glyph.minimum.y - margin, glyph.maximum.y + margin,
                               minimum, maximum) &&
        text_detail::clip_axis(origin.z, direction.z, glyph.minimum.z - margin, glyph.maximum.z + margin,
                               minimum, maximum);
}

TEXT_SOLID_HD inline bool text_solid_contains(const TextGlyph& glyph, const TextCurve* curves,
                                             SceneVector origin, SceneVector direction,
                                             float boundary, bool inclusive) {
    const float oz = origin.z - glyph.position.z;
    if (direction.z == 0) {
        if (oz <= 0 || oz >= glyph.depth) return false;
    } else {
        const float a = -oz / direction.z, b = (glyph.depth - oz) / direction.z;
        const float entry = fminf(a, b), exit = fmaxf(a, b);
        if (inclusive ? !(entry <= boundary && boundary < exit) : !(entry < boundary && boundary <= exit))
            return false;
    }
    const float ox = origin.x - glyph.position.x, oy = origin.y - glyph.position.y;
    if (direction.x == 0 && direction.y == 0) {
        return text_detail::stationary_contains_xy(glyph, curves, ox, oy);
    }
    return text_detail::ray_contains_xy(glyph, curves, ox, oy, direction.x, direction.y, boundary, inclusive);
}

#undef TEXT_SOLID_HD
