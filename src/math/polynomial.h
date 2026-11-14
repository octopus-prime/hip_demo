// Adapted from Boost.Math tools/{roots,cubic_roots,quartic_roots}.hpp.
// (C) Copyright John Maddock 2006.
// (C) Copyright Nick Thompson 2021.
// (C) Copyright Matt Borland 2024.
// Distributed under the Boost Software License, Version 1.0.
// See third_party/boost_math/LICENSE_1_0.txt.
#pragma once

#include <cmath>

#if defined(__HIPCC__)
#define POLYNOMIAL_HD __host__ __device__
#else
#define POLYNOMIAL_HD
#endif

namespace hip_demo {

// Sorted finite real roots, including multiplicities. An identically zero
// polynomial has no isolated roots and therefore returns an empty result.
struct PolynomialRoots {
    float values[4]{};
    int count = 0;

    POLYNOMIAL_HD void add(float value) {
        if (!std::isfinite(value)) return;
        int index = count;
        while (index > 0 && values[index - 1] > value) {
            values[index] = values[index - 1];
            --index;
        }
        values[index] = value;
        ++count;
    }
};

namespace polynomial_detail {
POLYNOMIAL_HD inline PolynomialRoots pair(float first, float second) {
    PolynomialRoots roots;
    const bool first_finite = std::isfinite(first), second_finite = std::isfinite(second);
    if (first_finite && second_finite) {
        roots.values[0] = fminf(first, second);
        roots.values[1] = fmaxf(first, second);
        roots.count = 2;
    } else if (first_finite || second_finite) {
        roots.values[0] = first_finite ? first : second;
        roots.count = 1;
    }
    return roots;
}

POLYNOMIAL_HD inline int scale_exponent(float magnitude) {
    int exponent = 0;
    if (magnitude != 0.0f && (magnitude < 1e-10f || magnitude > 1e10f))
        frexpf(magnitude, &exponent);
    return exponent;
}

POLYNOMIAL_HD inline float polish(float root, float f, float df, float d2f) {
    const float denominator = fmaf(2.0f * df, df, -f * d2f);
    if (denominator != 0.0f) return root - (2.0f * f * df) / denominator;
    return df != 0.0f ? root - f / df : root;
}
}

POLYNOMIAL_HD inline PolynomialRoots quadratic_roots(float a, float b, float c) {
    PolynomialRoots roots;
    if (a == 0.0f) {
        if (b != 0.0f) roots.add(-c / b);
        return roots;
    }
    const int exponent = polynomial_detail::scale_exponent(fmaxf(fabsf(a), fmaxf(fabsf(b), fabsf(c))));
    if (exponent != 0) {
        a = scalbnf(a, -exponent); b = scalbnf(b, -exponent); c = scalbnf(c, -exponent);
    }
    if (b == 0.0f) {
        const float square = -c / a;
        if (square >= 0.0f) {
            const float root = sqrtf(square);
            return polynomial_detail::pair(-root, root);
        }
        return roots;
    }
    const float product = 4.0f * a * c;
    const float discriminant = fmaf(b, b, -product) + fmaf(-c, 4.0f * a, product);
    if (discriminant < 0.0f) return roots;
    const float q = -0.5f * (b + copysignf(sqrtf(discriminant), b));
    return polynomial_detail::pair(q / a, c / q);
}

POLYNOMIAL_HD inline PolynomialRoots cubic_roots(float a, float b, float c, float d) {
    if (a == 0.0f) return quadratic_roots(b, c, d);
    if (d == 0.0f) {
        auto roots = quadratic_roots(a, b, c);
        roots.add(0.0f);
        return roots;
    }
    const int exponent = polynomial_detail::scale_exponent(
        fmaxf(fmaxf(fabsf(a), fabsf(b)), fmaxf(fabsf(c), fabsf(d))));
    if (exponent != 0) {
        a = scalbnf(a, -exponent); b = scalbnf(b, -exponent);
        c = scalbnf(c, -exponent); d = scalbnf(d, -exponent);
    }
    float p = b / a, q = c / a, r = d / a;
    const bool deflate = fabsf(p) > 16.0f &&
        fabsf(p) > 16.0f * fmaxf(sqrtf(fabsf(q)), cbrtf(fabsf(r)));
    float variable_scale = 1.0f;
    if (fabsf(p) > 32.0f || fabsf(q) > 1024.0f || fabsf(r) > 32768.0f) {
        int variable_exponent = 0;
        frexpf(fmaxf(fabsf(p), fmaxf(sqrtf(fabsf(q)), cbrtf(fabsf(r)))), &variable_exponent);
        variable_scale = scalbnf(1.0f, variable_exponent - 1);
        p /= variable_scale;
        q = q / variable_scale / variable_scale;
        r = r / variable_scale / variable_scale / variable_scale;
    }
    const float Q = fmaf(p, p, -3.0f * q) / 9.0f;
    const float R = fmaf(p, fmaf(2.0f * p, p, -9.0f * q), 27.0f * r) / 54.0f;
    const float discriminant = fmaf(R, R, -Q * Q * Q);
    PolynomialRoots candidates;
    if (discriminant < 0.0f) {
        const float rtQ = sqrtf(Q);
        const float theta = acosf(fmaxf(-1.0f, fminf(1.0f, R / (Q * rtQ)))) / 3.0f;
        const float st = sinf(theta), ct = cosf(theta);
        constexpr float sqrt3 = 1.7320508075688772f;
        candidates.add(-2.0f * rtQ * ct - p / 3.0f);
        candidates.add(-rtQ * (-ct + sqrt3 * st) - p / 3.0f);
        candidates.add(rtQ * (ct + sqrt3 * st) - p / 3.0f);
    } else {
        const float A = -copysignf(cbrtf(fabsf(R) + sqrtf(discriminant)), R);
        const float B = A != 0.0f ? Q / A : 0.0f;
        candidates.add(A + B - p / 3.0f);
        if (A == B || discriminant == 0.0f) {
            candidates.add(-A - p / 3.0f);
            candidates.add(-A - p / 3.0f);
        }
    }
    for (int i = 0; i < candidates.count; ++i) candidates.values[i] *= variable_scale;
    if (deflate && candidates.count != 0) {
        const float first = candidates.values[0], last = candidates.values[candidates.count - 1];
        const float large = fabsf(first) > fabsf(last) ? first : last;
        // Avoid cancellation of the two small roots when the cubic is nearly quadratic.
        const float constant = -d / large;
        candidates = quadratic_roots(a, (constant - c) / large, constant);
        candidates.add(large);
    }
    PolynomialRoots roots;
    for (int i = 0; i < candidates.count; ++i) {
        float x = candidates.values[i];
        const float f = fmaf(fmaf(fmaf(a, x, b), x, c), x, d);
        const float df = fmaf(fmaf(3.0f * a, x, 2.0f * b), x, c);
        const float d2f = fmaf(6.0f * a, x, 2.0f * b);
        const float corrected = polynomial_detail::polish(x, f, df, d2f);
        if (fabsf(fmaf(fmaf(fmaf(a, corrected, b), corrected, c), corrected, d)) <= fabsf(f))
            x = corrected;
        roots.add(x);
    }
    return roots;
}

POLYNOMIAL_HD inline PolynomialRoots quartic_roots(float a, float b, float c, float d, float e) {
    if (a == 0.0f) return cubic_roots(b, c, d, e);
    const int exponent = polynomial_detail::scale_exponent(
        fmaxf(fmaxf(fabsf(a), fabsf(b)), fmaxf(fabsf(c), fmaxf(fabsf(d), fabsf(e)))));
    if (exponent != 0) {
        a = scalbnf(a, -exponent); b = scalbnf(b, -exponent); c = scalbnf(c, -exponent);
        d = scalbnf(d, -exponent); e = scalbnf(e, -exponent);
    }
    PolynomialRoots candidates;
    if (e == 0.0f) {
        candidates = cubic_roots(a, b, c, d);
        candidates.add(0.0f);
    } else {
        const float A = b / a, B = c / a, C = d / a, D = e / a;
        const float square = A * A;
        const float p = fmaf(-0.375f, square, B);
        const float q = fmaf(0.125f * square, A, fmaf(-0.5f * A, B, C));
        const float r = fmaf(-0.01171875f * square, square,
            fmaf(0.0625f * square, B, fmaf(-0.25f * A, C, D)));
        const float shift = -0.25f * A;
        if (r == 0.0f) {
            const auto reduced = cubic_roots(1.0f, 0.0f, p, q);
            for (int i = 0; i < reduced.count; ++i) candidates.add(reduced.values[i] + shift);
            candidates.add(shift);
        } else if (q == 0.0f) {
            const auto squares = quadratic_roots(1.0f, p, r);
            for (int i = 0; i < squares.count; ++i) {
                if (squares.values[i] < 0.0f) continue;
                const float root = sqrtf(squares.values[i]);
                candidates.add(shift - root); candidates.add(shift + root);
            }
        } else {
            const auto resolvent = cubic_roots(1.0f, 2.0f * p, fmaf(p, p, -4.0f * r), -q * q);
            if (resolvent.count == 0 || resolvent.values[resolvent.count - 1] <= 0.0f) return {};
            const float z = resolvent.values[resolvent.count - 1], s = sqrtf(z);
            const float v = 0.5f * (p + z + q / s), u = v - q / s;
            const auto first = quadratic_roots(1.0f, s, u);
            const auto second = quadratic_roots(1.0f, -s, v);
            for (int i = 0; i < first.count; ++i) candidates.add(first.values[i] + shift);
            for (int i = 0; i < second.count; ++i) candidates.add(second.values[i] + shift);
        }
    }
    PolynomialRoots roots;
    for (int i = 0; i < candidates.count; ++i) {
        float x = candidates.values[i];
        const float f = fmaf(fmaf(fmaf(fmaf(a, x, b), x, c), x, d), x, e);
        const float df = fmaf(fmaf(fmaf(4.0f * a, x, 3.0f * b), x, 2.0f * c), x, d);
        const float d2f = fmaf(fmaf(12.0f * a, x, 6.0f * b), x, 2.0f * c);
        const float corrected = polynomial_detail::polish(x, f, df, d2f);
        if (fabsf(fmaf(fmaf(fmaf(fmaf(a, corrected, b), corrected, c), corrected, d), corrected, e)) <= fabsf(f))
            x = corrected;
        roots.add(x);
    }
    return roots;
}

}

#undef POLYNOMIAL_HD
