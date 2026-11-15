#pragma once

#include "polynomial_cases.h"

__global__ void check_analytic_intersections(unsigned int* failures) {
    using namespace hip_demo;
    failures[0] = polynomial_failures();
    struct TorusCase {
        Ray ray;
        bool found;
        float distance;
        float3 normal;
        float maximum = 100000.0f;
    };
    const TorusCase cases[]{
        {{{4, 0, 0}, {-1, 0, 0}}, true, 1.5f, {1, 0, 0}},
        {{{4, 0, 0}, {-2, 0, 0}}, true, 0.75f, {1, 0, 0}},
        {{{0, 0, 0}, {1, 0, 0}}, true, 1.5f, {-1, 0, 0}},
        {{{2, 0, 0}, {1, 0, 0}}, true, 0.5f, {1, 0, 0}},
        {{{2, 3, 0}, {0, -1, 0}}, true, 2.5f, {0, 1, 0}},
        {{{2.5f, 3, 0}, {0, -1, 0}}, true, 3, {1, 0, 0}},
        {{{4, 0, 2.5f}, {-1, 0, 0}}, true, 4, {0, 0, 1}},
        {{{0, 3, 0}, {0, -1, 0}}, false, 0, {}},
        {{{2.5001f, 3, 0}, {0, -1, 0}}, false, 0, {}},
        {{{4, 0, 0}, {-1, 0, 0}}, false, 0, {}, 1.4999f},
        {{{4, 0, 0}, {-1, 0, 0}}, true, 1.5f, {1, 0, 0}, 1.5001f},
        {{{2.5f, 0, 0}, {-1, 0, 0}}, true, 1, {-1, 0, 0}},
        {{{1.5f, 0, 0}, {1, 0, 0}}, true, 1, {1, 0, 0}},
        {{{10000, 0, 0}, {-1, 0, 0}}, true, 9997.5f, {1, 0, 0}},
        {{{4, 0.3f, 0}, {-1, 0, 0}}, true, 1.6f, {0.8f, 0.6f, 0}},
        {{{4, 0.5f, 0}, {-1, 0, 0}}, true, 2, {0, 1, 0}},
        {{{4, 0.5001f, 0}, {-1, 0, 0}}, false, 0, {}},
    };
    unsigned int failed = 0;
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const auto& test = cases[i];
        Hit hit{};
        const bool found = Torus{{}, 2.0f, 0.5f}.intersect(test.ray, 0.001f, test.maximum, hit);
        if (found != test.found || (found &&
            (fabsf(hit.distance - test.distance) > 0.0002f ||
             fabsf(dot(hit.normal, test.normal)) < 0.9999f ||
             fabsf(torus_distance(hit.point, {}, 2.0f, 0.5f)) > 0.00002f)))
            failed |= 1u << i;
    }
    Hit hit{};
    if (!Torus{{3, 4, 5}, 2, 0.5f}.intersect({{7, 4, 5}, {-1, 0, 0}}, 0.001f, 10, hit) ||
        fabsf(hit.distance - 1.5f) > 0.00002f) failed |= 1u << 17;
    const Sphere sphere{{}, 1};
    if (!sphere.intersect({{0, 0, 4}, {0, 0, -2}}, 0.001f, 10, hit) ||
        fabsf(hit.distance - 1.5f) > 0.00001f || hit.normal.z < 0.9999f ||
        !sphere.intersect({{0, 0, 0}, {0, 0, 2}}, 0.001f, 10, hit) ||
        fabsf(hit.distance - 0.5f) > 0.00001f ||
        !sphere.intersect({{1, 0, 4}, {0, 0, -1}}, 0.001f, 10, hit) ||
        fabsf(hit.distance - 4.0f) > 0.00001f ||
        sphere.intersect({{1.0001f, 0, 4}, {0, 0, -1}}, 0.001f, 10, hit))
        failed |= 1u << 18;
    // Independent parametric torus points and normals, including oblique/non-unit rays.
    for (int ring = 0; ring < 16; ++ring) {
        for (int tube = 0; tube < 16; ++tube) {
            const float theta = (ring + 0.3f) * 0.3926990817f;
            const float phi = (tube + 0.2f) * 0.3926990817f;
            const float3 radial{cosf(theta), 0, sinf(theta)};
            const float3 normal = cosf(phi) * radial + float3{0, sinf(phi), 0};
            const float3 point = 2.0f * radial + 0.5f * normal;
            for (int oblique = 0; oblique < 2; ++oblique) {
                const float3 displacement = 0.25f * normal +
                    (oblique ? 0.2f : 0.0f) * float3{-radial.z, 0, radial.x};
                const Ray ray{point + displacement, -2.0f * displacement};
                if (!Torus{{}, 2, 0.5f}.intersect(ray, 0.001f, 10, hit) ||
                    fabsf(hit.distance - 0.5f) > 0.0001f || dot(hit.normal, normal) < 0.9999f)
                    failed |= 1u << 19;
            }
        }
    }
    failures[1] = failed;
}

inline void run_analytic_intersection_tests() {
    auto failure = hip_demo::allocate_device<unsigned int>(2, "Allocating analytic intersection checks");
    hipLaunchKernelGGL(check_analytic_intersections, dim3(1), dim3(1), 0, 0, failure.get());
    hip_demo::check_hip(hipGetLastError(), "Launching analytic intersection checks");
    unsigned int results[2]{};
    hip_demo::check_hip(hipMemcpy(results, failure.get(), sizeof(results), hipMemcpyDeviceToHost),
                        "Reading analytic intersection checks");
    if (results[0] != 0 || results[1] != 0)
        throw std::runtime_error("Analytic intersection checks failed (polynomial flags " +
            std::to_string(results[0]) + ", surface flags " + std::to_string(results[1]) + ")");
    std::cout << "Analytic float polynomial, sphere and torus checks passed.\n";
}
