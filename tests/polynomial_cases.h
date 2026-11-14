#pragma once

#include "math/polynomial.h"

#if defined(__HIPCC__)
#define POLYNOMIAL_TEST_HD __host__ __device__
#else
#define POLYNOMIAL_TEST_HD
#endif

POLYNOMIAL_TEST_HD inline unsigned int polynomial_failures() {
    struct Case {
        int degree;
        float coefficients[5];
        int count;
        float expected[4];
        float tolerance = 2e-5f;
    };
    const Case cases[]{
        {2, {1, -3, 2}, 2, {1, 2}},
        {2, {1, -2, 1}, 2, {1, 1}},
        {2, {1, 0, 1}, 0, {}},
        {2, {0, 2, -1}, 1, {0.5f}},
        {2, {0, 0, 1}, 0, {}},
        {2, {0, 0, 0}, 0, {}},
        {2, {1e-20f, -3e-20f, 2e-20f}, 2, {1, 2}},
        {2, {1e20f, -3e20f, 2e20f}, 2, {1, 2}},
        {2, {1, -10000, 1}, 2, {0.0001f, 10000}},
        {2, {1, -2, 0.9999847412109375f}, 2, {0.99609375f, 1.00390625f}, 2e-6f},
        {3, {1, -1.5f, 0.6875f, -0.09375f}, 3, {0.25f, 0.5f, 0.75f}},
        {3, {1, -1.5f, 0.75f, -0.125f}, 3, {0.5f, 0.5f, 0.5f}},
        {3, {1, 0, -3, -2}, 3, {-1, -1, 2}},
        {3, {1, 0, 0, 1}, 1, {-1}},
        {3, {1, 0, 1, 1}, 1, {-0.6823278f}},
        {3, {0, 1, -3, 2}, 2, {1, 2}},
        {3, {1, -1, 0, 0}, 3, {0, 0, 1}},
        {3, {1e-20f, 0, 0, -1e-20f}, 1, {1}},
        {4, {1, -1, -7, 1, 6}, 4, {-2, -1, 1, 3}},
        {4, {1, -10, 35, -50, 24}, 4, {1, 2, 3, 4}},
        {4, {1, -4, 6, -4, 1}, 4, {1, 1, 1, 1}},
        {4, {1, 0, -9, -4, 12}, 4, {-2, -2, 1, 3}},
        {4, {1, 0, 1, 0, -2}, 2, {-1, 1}},
        {4, {1, 0, 0, 0, 1}, 0, {}},
        {4, {1, 0, 0, 1, -2}, 2, {-1.35320996f, 1}},
        {4, {1, -6, 11, -6, 0}, 4, {0, 1, 2, 3}},
        {4, {0, 1, 0, -3, -2}, 3, {-1, -1, 2}},
        {4, {1e20f, 0, 1e20f, 0, -2e20f}, 2, {-1, 1}},
        {3, {0.0000152587890625f, 0.9999847412109375f, -0.9999971389770508f, 0.1875f},
            3, {-65536, 0.25f, 0.75f}, 2e-6f},
        {3, {1e-10f, 1, -1, 0.1875f}, 3, {-1e10f, 0.25f, 0.75f}, 2e-6f},
    };
    static_assert(sizeof(cases) / sizeof(cases[0]) <= 32);
    unsigned int failures = 0;
    for (unsigned int index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const auto& test = cases[index];
        const auto* p = test.coefficients;
        const auto roots = test.degree == 2 ? hip_demo::quadratic_roots(p[0], p[1], p[2]) :
            test.degree == 3 ? hip_demo::cubic_roots(p[0], p[1], p[2], p[3]) :
                               hip_demo::quartic_roots(p[0], p[1], p[2], p[3], p[4]);
        bool failed = roots.count != test.count;
        for (int root = 0; root < roots.count && root < test.count; ++root) {
            const float x = roots.values[root];
            if (!std::isfinite(x) || fabsf(x - test.expected[root]) >
                test.tolerance * fmaxf(1.0f, fabsf(test.expected[root]))) failed = true;
            float residual = p[0], magnitude = fabsf(p[0]);
            for (int i = 1; i <= test.degree; ++i) {
                residual = fmaf(residual, x, p[i]);
                magnitude = fmaf(magnitude, fabsf(x), fabsf(p[i]));
            }
            if (fabsf(residual) > 2e-6f * magnitude) failed = true;
        }
        if (failed) failures |= 1u << index;
    }
    return failures;
}

#undef POLYNOMIAL_TEST_HD
