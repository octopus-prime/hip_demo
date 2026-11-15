#pragma once

#include "components/geometry/csg/intersection.h"

namespace hip_demo {
struct CsgQuadricProbe {
    unsigned int object;
    Ray ray;
    float minimum, maximum;
    bool found;
    float distance;
    float3 outward;
};

__global__ void check_csg_quadrics(TraceScene scene, const GpuCsgLeaf* leaves, const CsgQuadricProbe* probes,
                                   unsigned int count, unsigned int* failures) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    auto test = probes[i];
    test.ray.origin.x += 10 * test.object;
    const auto& object = scene.csgs[test.object];
    Hit direct{}, cached{}, reference{};
    bool usable = false;
    const bool found = intersect_csg(test.ray, object, leaves, test.minimum, test.maximum, direct);
    const bool cached_found = intersect_csg_cached(test.ray, object, leaves,
                                                   test.minimum, test.maximum, cached, {}, usable);
    const bool reference_found = intersect_csg_uncached(test.ray, object, leaves,
                                                        test.minimum, test.maximum, reference);
    hiprtRay ray{};
    ray.origin = test.ray.origin; ray.direction = test.ray.direction;
    ray.minT = test.minimum; ray.maxT = test.maximum;
    hiprtGeomCustomTraversalClosest traversal(scene.geometry, ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const auto actual = traversal.getNextHit();
    const float3 outward = direct.front_face ? direct.normal : -direct.normal;
    const bool failed = !usable || found != test.found || cached_found != found ||
        reference_found != found || actual.hasHit() != found || (found &&
        (fabsf(direct.distance - test.distance) > 0.00005f ||
         fabsf(cached.distance - direct.distance) > 0.00005f ||
         fabsf(reference.distance - direct.distance) > 0.00005f ||
         fabsf(actual.t - direct.distance) > 0.00005f ||
         dot(outward, normalize(test.outward)) < 0.9999f ||
         dot(cached.normal, direct.normal) < 0.9999f ||
         dot(reference.normal, direct.normal) < 0.9999f ||
         dot(actual.normal, normalize(test.outward)) < 0.9999f ||
         actual.primID != test.object));
    if (failed)
        printf("CSG quadric probe %u: direct=%d t=%g normal=(%g,%g,%g), cached=%d t=%g, reference=%d t=%g, HIP=%d t=%g\n",
               i, found, direct.distance, outward.x, outward.y, outward.z,
               cached_found, cached.distance, reference_found, reference.distance, actual.hasHit(), actual.t);
    failures[i] = failed;
}

inline void run_csg_quadric_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    const Material material = Phong{ConstantColor{{0.2f, 0.5f, 0.7f}}};
    const auto leaf = [](auto geometry) { return CsgNode{geometry, CsgOperation::unite, {}}; };
    const auto difference = [](CsgNode a, CsgNode b) {
        return CsgNode{Sphere{}, CsgOperation::difference, {std::move(a), std::move(b)}};
    };
    description.objects = {
        {Csg{difference(leaf(Cylinder{{0, 2, 0}, 1, 2}), leaf(Cylinder{{0, 2, 0}, 0.4f, 4}))}, material},
        {Csg{leaf(Cone{{10, 2, 0}, 1, 2})}, material},
        {Csg{leaf(Paraboloid{{20, 2, 0}, 1, 2})}, material},
        {Csg{leaf(Hyperboloid{{30, 2, 0}, 0.6f, 1, 2})}, material},
        {Csg{leaf(Ellipsoid{{40, 2, 0}, {0.75f, 1.5f, 0.5f}})}, material},
        {Csg{difference(leaf(Cylinder{{50, 2, 0}, 1, 2}), leaf(Cylinder{{50, 2, 0}, 1, 2}))}, material}
    };
    const std::vector<CsgQuadricProbe> probes{
        {0, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, true, 2, {0, 0, 1}},
        {0, {{0, 2, 3}, {0, 0, -1}}, 2.1f, 8, true, 2.6f, {0, 0, -1}},
        {0, {{0.7f, 5, 0}, {0, -1, 0}}, .001f, 8, true, 2, {0, 1, 0}},
        {0, {{0, 5, 0}, {0, -1, 0}}, .001f, 8, false, 0, {}},
        {0, {{2, 4, 0}, {-1, -1, 0}}, .001f, 8, true, 1, {0, 1, 0}},
        {0, {{2, 2, 0}, {-1, 1, 0}}, .001f, 8, false, 0, {}},
        {0, {{0, 3.5f, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}},
        {0, {{0, 3, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}},
        {1, {{0, 4, 0}, {0, -1, 0}}, .001f, 8, true, 1, {0, 1, 0}},
        {1, {{0, 4, 0}, {0, -1, 0}}, 1.1f, 8, true, 3, {0, -1, 0}},
        {1, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, true, 2.5f, {0, .5f, 1}},
        {1, {{0, 3.5f, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}},
        {2, {{0, 0, 0}, {0, 1, 0}}, .001f, 8, true, 1, {0, -1, 0}},
        {2, {{.5f, 4, 0}, {0, -1, 0}}, .001f, 8, true, 1, {0, 1, 0}},
        {2, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, true, 2.2928932f, {0, -.5f, 1.4142136f}},
        {3, {{.7f, 4, 0}, {0, -1, 0}}, .001f, 8, true, 1, {0, 1, 0}},
        {3, {{.7f, 4, 0}, {0, -1, 0}}, 1.1f, 8, true, 1.399075f, {3.888889f, -1.20185f, 0}},
        {3, {{.7f, 4, 0}, {0, -1, 0}}, 1.5f, 8, true, 2.600925f, {3.888889f, 1.20185f, 0}},
        {3, {{.7f, 4, 0}, {0, -1, 0}}, 2.7f, 8, true, 3, {0, -1, 0}},
        {3, {{0, 3.5f, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}},
        {4, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, true, 2.5f, {0, 0, 1}},
        {5, {{0, 4, 0}, {0, -1, 0}}, .001f, 8, false, 0, {}},
        {5, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}}
    };
    const auto prepared = prepare_scene(description);
    const HiprtScene scene(device, prepared);
    auto leaves = allocate_device<GpuCsgLeaf>(prepared.csg_leaves.size(), "Allocating CSG quadric leaves");
    check_hip(hipMemcpy(leaves.get(), prepared.csg_leaves.data(), prepared.csg_leaves.size() * sizeof(GpuCsgLeaf),
                        hipMemcpyHostToDevice), "Uploading CSG quadric leaves");
    auto input = allocate_device<CsgQuadricProbe>(probes.size(), "Allocating CSG quadric probes");
    auto failures = allocate_device<unsigned int>(probes.size(), "Allocating CSG quadric failures");
    check_hip(hipMemcpy(input.get(), probes.data(), probes.size() * sizeof(CsgQuadricProbe), hipMemcpyHostToDevice),
              "Uploading CSG quadric probes");
    hipLaunchKernelGGL(check_csg_quadrics, dim3(1), dim3(64), 0, 0, scene.scene(), leaves.get(), input.get(),
                       static_cast<unsigned int>(probes.size()), failures.get());
    check_hip(hipGetLastError(), "Launching CSG quadric checks");
    std::vector<unsigned int> results(probes.size());
    check_hip(hipMemcpy(results.data(), failures.get(), results.size() * sizeof(unsigned int), hipMemcpyDeviceToHost),
              "Reading CSG quadric checks");
    for (std::size_t i = 0; i < results.size(); ++i)
        if (results[i]) throw std::runtime_error("CSG quadric probe " + std::to_string(i) + " failed");
    std::cout << "CSG quadric capped boundaries, parity, normals, intervals and HIP RT checks passed.\n";
}
}
