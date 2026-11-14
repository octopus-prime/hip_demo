#pragma once

#include "surface/text.h"

#if defined(__HIPCC__)
#define TEXT_SOLID_HD __host__ __device__
#else
#define TEXT_SOLID_HD
#endif

namespace text_detail {

TEXT_SOLID_HD inline bool filled(const TextGlyph& glyph, int winding) {
    return glyph.even_odd ? winding % 2 != 0 : winding != 0;
}

TEXT_SOLID_HD inline bool ray_contains_xy(const TextGlyph& glyph, const TextCurve* curves,
                                          float ox, float oy, float dx, float dy,
                                          float boundary, bool inclusive) {
    int positive = 0, negative = 0;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto& curve = curves[glyph.first_curve + index];
        float p[4];
        for (int i = 0; i < 4; ++i) p[i] = fmaf(curve.x[i], dy, -curve.y[i] * dx);
        p[0] += fmaf(oy, dx, -ox * dy);
        float cuts[4]{0, 0, 0, 1};
        const int critical = quadratic_roots(3 * p[3], 2 * p[2], p[1], cuts + 1);
        cuts[critical + 1] = 1;
        float roots[4];
        const int count = curve_roots(p, roots);
        for (int part = 0; part <= critical; ++part) {
            const float low = cuts[part], high = cuts[part + 1];
            const float start = evaluate(p, low), end = evaluate(p, high);
            const bool upward = end > start;
            // Both transverse limits must be filled: travelling along a contour
            // is not a solid interval. The paired half-open rules also cancel tangencies.
            const bool plus = upward ? start <= 0 && 0 < end : end <= 0 && 0 < start;
            const bool minus = upward ? start < 0 && 0 <= end : end < 0 && 0 <= start;
            if (!plus && !minus) continue;
            for (int root = 0; root < count; ++root) {
                constexpr float tolerance = 8.0f * 0x1p-23f;
                if (roots[root] < low - tolerance || roots[root] > high + tolerance) continue;
                const float u = fmaxf(low, fminf(high, roots[root]));
                const float t = fabsf(dx) >= fabsf(dy)
                    ? (evaluate(curve.x, u) - ox) / dx : (evaluate(curve.y, u) - oy) / dy;
                if (inclusive ? t <= boundary : t < boundary) {
                    if (plus) positive += upward ? 1 : -1;
                    if (minus) negative += upward ? 1 : -1;
                }
                break;
            }
        }
    }
    return filled(glyph, positive) && filled(glyph, negative);
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
        return text_detail::ray_contains_xy(glyph, curves, ox, oy, 1, 0, 0, false) &&
            text_detail::ray_contains_xy(glyph, curves, ox, oy, 1, 0, 0, true);
    }
    return text_detail::ray_contains_xy(glyph, curves, ox, oy, direction.x, direction.y, boundary, inclusive);
}

#undef TEXT_SOLID_HD
