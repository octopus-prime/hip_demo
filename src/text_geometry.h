#pragma once

#include "scene_description.h"

#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__HIPCC__)
#define TEXT_HD __host__ __device__
#else
#define TEXT_HD
#endif

struct TextCurve {
    double x[4]{}, y[4]{};
};

struct TextGlyph {
    SceneVector position, minimum, maximum;
    SceneMaterial material;
    float depth = 0, normal_sign = 1;
    std::uint32_t first_curve = 0, curve_count = 0, object_id = 0;
    bool even_odd = false;
};

struct TextGeometry {
    std::vector<TextCurve> curves;
    std::vector<TextGlyph> glyphs;
};

void append_text_geometry(TextGeometry& geometry, const SceneText& text, std::uint32_t object_id);

namespace text_detail {
TEXT_HD inline double evaluate(const double* p, double u) {
    return ((p[3] * u + p[2]) * u + p[1]) * u + p[0];
}

TEXT_HD inline double derivative(const double* p, double u) {
    return (3 * p[3] * u + 2 * p[2]) * u + p[1];
}

TEXT_HD inline void add_root(double* roots, int& count, double u) {
    if (u < -1e-12 || u > 1 + 1e-12) return;
    u = fmax(0.0, fmin(1.0, u));
    int index = 0;
    while (index < count && roots[index] < u) ++index;
    if ((index < count && fabs(roots[index] - u) < 1e-9) ||
        (index > 0 && fabs(roots[index - 1] - u) < 1e-9)) return;
    for (int i = count; i > index; --i) roots[i] = roots[i - 1];
    roots[index] = u;
    ++count;
}

TEXT_HD inline int quadratic_roots(double a, double b, double c, double* roots) {
    const double scale = fmax(fabs(a), fmax(fabs(b), fabs(c)));
    if (scale == 0) return 0;
    a /= scale;
    b /= scale;
    c /= scale;
    int count = 0;
    if (fabs(a) < 1e-14) {
        if (fabs(b) >= 1e-14) add_root(roots, count, -c / b);
        return count;
    }
    const double discriminant = b * b - 4 * a * c;
    if (discriminant < 0) return 0;
    const double q = -0.5 * (b + copysign(sqrt(discriminant), b));
    if (q == 0) {
        add_root(roots, count, -b / (2 * a));
    } else {
        add_root(roots, count, q / a);
        add_root(roots, count, c / q);
    }
    return count;
}

TEXT_HD inline int curve_roots(const double* coefficients, double* roots) {
    const double scale = fmax(fmax(fabs(coefficients[0]), fabs(coefficients[1])),
                              fmax(fabs(coefficients[2]), fabs(coefficients[3])));
    if (scale == 0) return 0;
    double p[4];
    for (int i = 0; i < 4; ++i) p[i] = coefficients[i] / scale;
    if (fabs(p[3]) < 1e-14) return quadratic_roots(p[2], p[1], p[0], roots);
    double cuts[4]{0, 0, 0, 1};
    const int critical = quadratic_roots(3 * p[3], 2 * p[2], p[1], cuts + 1);
    cuts[critical + 1] = 1;
    int count = 0;
    // Derivative roots split the cubic into monotone intervals, retaining tangent roots.
    for (int i = 0; i <= critical + 1; ++i) {
        if (fabs(evaluate(p, cuts[i])) <= 1e-13) add_root(roots, count, cuts[i]);
        if (i == critical + 1) break;
        double low = cuts[i], high = cuts[i + 1];
        const double a = evaluate(p, low), b = evaluate(p, high);
        if (fabs(a) <= 1e-13 || fabs(b) <= 1e-13 || (a < 0) == (b < 0)) continue;
        for (int step = 0; step < 48; ++step) {
            const double middle = (low + high) * 0.5;
            if ((evaluate(p, middle) < 0) == (a < 0)) low = middle;
            else high = middle;
        }
        add_root(roots, count, (low + high) * 0.5);
    }
    return count;
}

TEXT_HD inline bool contains(const TextGlyph& glyph, const TextCurve* curves, double x, double y) {
    int winding = 0;
    for (std::uint32_t index = 0; index < glyph.curve_count; ++index) {
        const auto& curve = curves[glyph.first_curve + index];
        double cuts[4]{0, 0, 0, 1};
        const int critical = quadratic_roots(3 * curve.y[3], 2 * curve.y[2], curve.y[1], cuts + 1);
        cuts[critical + 1] = 1;
        for (int part = 0; part <= critical; ++part) {
            double low = cuts[part], high = cuts[part + 1];
            const double start = evaluate(curve.y, low), end = evaluate(curve.y, high);
            const bool upward = end > start;
            // Half-open intervals count shared vertices once; extrema do not toggle the fill.
            if (!(upward ? start <= y && y < end : end <= y && y < start)) continue;
            for (int step = 0; step < 48; ++step) {
                const double middle = (low + high) * 0.5;
                if ((evaluate(curve.y, middle) < y) == upward) low = middle;
                else high = middle;
            }
            if (evaluate(curve.x, (low + high) * 0.5) > x) winding += upward ? 1 : -1;
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
    const double ox = static_cast<double>(origin.x) - glyph.position.x;
    const double oy = static_cast<double>(origin.y) - glyph.position.y;
    const double oz = static_cast<double>(origin.z) - glyph.position.z;
    double closest = maximum;
    bool found = false;
    if (direction.z != 0) {
        for (int cap = 0; cap < 2; ++cap) {
            const double t = ((cap == 0 ? 0.0 : glyph.depth) - oz) / direction.z;
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
            double p[4];
            for (int i = 0; i < 4; ++i) p[i] = curve.x[i] * direction.y - curve.y[i] * direction.x;
            p[0] += oy * direction.x - ox * direction.y;
            double roots[4];
            const int count = text_detail::curve_roots(p, roots);
            for (int root = 0; root < count; ++root) {
                const double u = roots[root];
                const double t = fabs(direction.x) >= fabs(direction.y)
                    ? (text_detail::evaluate(curve.x, u) - ox) / direction.x
                    : (text_detail::evaluate(curve.y, u) - oy) / direction.y;
                const double z = oz + t * direction.z;
                if (t < minimum || t >= closest || z < 0 || z > glyph.depth) continue;
                const double dx = text_detail::derivative(curve.x, u);
                const double dy = text_detail::derivative(curve.y, u);
                const double length = sqrt(dx * dx + dy * dy);
                if (length == 0) continue;
                closest = t;
                hit.normal = {static_cast<float>(glyph.normal_sign * dy / length),
                              static_cast<float>(-glyph.normal_sign * dx / length), 0};
                found = true;
            }
        }
    }
    if (found) hit.distance = static_cast<float>(closest);
    return found;
}

#undef TEXT_HD
