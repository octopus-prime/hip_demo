#pragma once

#include "components/geometry/csg/intersection.h"
#include <filesystem>

namespace hip_demo {

__global__ void check_csg(TraceScene scene, const GpuCsg* objects, const GpuCsgLeaf* leaves,
                          unsigned int* failures) {
    struct Probe {
        unsigned int object;
        Ray ray;
        float minimum, maximum;
        bool found;
        float distance;
        float3 outward;
        bool front;
    };
    const Probe probes[]{
        {0, {{-3, 1, 0}, {1, 0, 0}}, 0.001f, 4, true, 1.5f, {-1, 0, 0}, true},
        {0, {{0, 1, 0}, {1, 0, 0}}, 0.001f, 3, true, 1.5f, {1, 0, 0}, false},
        {0, {{-3, 1, 0}, {2, 0, 0}}, 0.001f, 2, true, 0.75f, {-1, 0, 0}, true},
        {0, {{-3, 1, 0}, {1, 0, 0}}, 2, 5, true, 4.5f, {1, 0, 0}, false},
        {0, {{-3, 1, 0}, {1, 0, 0}}, 1.5f, 2, true, 1.5f, {-1, 0, 0}, true},
        {0, {{-3, 1, 0}, {1, 0, 0}}, 0.001f, 1.5f, false, 0, {}, false},
        {0, {{-3, 2, 0}, {1, 0, 0}}, 0.001f, 5, false, 0, {}, false},
        {1, {{2, 1, 0}, {1, 0, 0}}, 0.001f, 3, true, 1.5f, {-1, 0, 0}, true},
        {1, {{4, 1, 0}, {1, 0, 0}}, 0.001f, 2, true, 0.5f, {1, 0, 0}, false},
        {1, {{4, 1, 3}, {0, 0, -1}}, 0.001f, 5, true, 2.1339746f, {-0.5f, 0, 0.8660254f}, true},
        {2, {{8, 1, 3}, {0, 0, -1}}, 0.001f, 5, true, 2, {0, 0, 1}, true},
        {2, {{8, 1, 0}, {0, 0, 1}}, 0.001f, 2, true, 0.5f, {0, 0, -1}, true},
        {2, {{8, 1, 0.75f}, {0, 0, -1}}, 0.001f, 2, true, 0.25f, {0, 0, -1}, false},
        {2, {{8, 1, 3}, {0, 0, -1}}, 2.1f, 4, true, 2.5f, {0, 0, -1}, false},
        {2, {{8, 1, 3}, {0, 0, -1}}, 2.6f, 4, true, 3.5f, {0, 0, 1}, true},
        {3, {{12, 1, 3}, {0, 0, -1}}, 0.001f, 5, false, 0, {}, false},
        {4, {{-2, 1, 0}, {-1, 0, 0}}, 0.001f, 5, true, 2.75f, {1, 0, 0}, true},
        {4, {{-4, 1, 3}, {0, 0, -1}}, 0.001f, 5, false, 0, {}, false},
        {5, {{-11, 1, 0}, {1, 0, 0}}, 0.001f, 4, true, 1.75f, {-1, 0, 0}, true},
        {6, {{0.5f, 4, 3}, {0, 0, -1}}, 0.001f, 4, true, 2, {0, 0, 1}, true},
        {7, {{-3, 4, -5}, {1, 0, 0}}, 0.001f, 7, false, 0, {}, false},
        {8, {{3, 4, -5}, {1, 0, 0}}, 0.001f, 8, true, 2, {-1, 0, 0}, true},
        {8, {{3, 4, -5}, {1, 0, 0}}, 2.1f, 8, true, 6, {1, 0, 0}, false},
    };
    unsigned int failed = 0;
    for (unsigned int i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const auto& test = probes[i];
        Hit hit{};
        const bool found = intersect_csg(test.ray, objects[test.object], leaves,
                                         test.minimum, test.maximum, hit);
        hiprtRay ray{};
        ray.origin = test.ray.origin; ray.direction = test.ray.direction;
        ray.minT = test.minimum; ray.maxT = test.maximum;
        hiprtGeomCustomTraversalClosest traversal(
            scene.geometry, ray, hiprtTraversalHintDefault, nullptr, scene.functions);
        const auto actual = traversal.getNextHit();
        const float3 outward = hit.front_face ? hit.normal : -hit.normal;
        if (found != test.found || actual.hasHit() != test.found || (found &&
            (fabsf(hit.distance - test.distance) > 0.00005f || hit.front_face != test.front ||
             dot(outward, test.outward) < 0.9999f ||
             fabsf(actual.t - test.distance) > 0.00005f ||
             actual.primID != scene.primitive_count + test.object ||
             dot(actual.normal, test.outward) < 0.9999f))) {
            printf("CSG probe %u failed: direct=%d t=%g, HIP RT=%d t=%g\n",
                   i, found, hit.distance, actual.hasHit(), actual.t);
            failed |= 1u << i;
        }
    }
    Hit shaded{};
    if (!hit_custom_objects({{8, 1, 0}, {0, 0, 1}}, shaded, scene) ||
        shaded.object_id != scene.primitive_count + 2 || shaded.material != ShaderKind::phong ||
        shaded.albedo.z != 0.9f || !shaded.front_face || shaded.normal.z > -0.999f)
        failed |= 1u << 30;
    if (!hit_custom_objects({{10, 3, 4}, {0, 0, -1}}, shaded, scene) ||
        shaded.object_id != 0 || fabsf(shaded.distance - 0.8f) > 0.00001f)
        failed |= 1u << 31;
    *failures = failed;
}

inline void run_csg_tests(int device, const std::filesystem::path& directory) {
    const auto description = load_scene_description((directory / "csg.hipscene").string());
    const auto prepared = prepare_scene(description);
    const HiprtScene scene(device, prepared);
    auto objects = allocate_device<GpuCsg>(prepared.csgs.size(), "Allocating CSG test objects");
    auto leaves = allocate_device<GpuCsgLeaf>(prepared.csg_leaves.size(), "Allocating CSG test leaves");
    check_hip(hipMemcpy(objects.get(), prepared.csgs.data(), prepared.csgs.size() * sizeof(GpuCsg),
                       hipMemcpyHostToDevice), "Uploading CSG test objects");
    check_hip(hipMemcpy(leaves.get(), prepared.csg_leaves.data(), prepared.csg_leaves.size() * sizeof(GpuCsgLeaf),
                       hipMemcpyHostToDevice), "Uploading CSG test leaves");
    auto failure = allocate_device<unsigned int>(1, "Allocating CSG test result");
    hipLaunchKernelGGL(check_csg, dim3(1), dim3(1), 0, 0, scene.scene(), objects.get(), leaves.get(), failure.get());
    check_hip(hipGetLastError(), "Launching CSG tests");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost), "Reading CSG tests");
    if (result != 0) throw std::runtime_error("CSG tests failed (flags " + std::to_string(result) + ")");
    std::cout << "Analytic CSG Boolean, interval, normal and HIP RT checks passed.\n";
}

}
