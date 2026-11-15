#pragma once

#include "gpu/scene.h"
#include "components/geometry/quadric/intersection.h"
#include <iostream>

namespace hip_demo {
struct QuadricProbe {
    unsigned int object;
    Ray ray;
    bool found;
    float distance;
    float3 outward;
    bool front = true;
    float minimum = 0.001f, maximum = 10;
    float tolerance = 0.00005f;
};

__global__ void check_quadrics(TraceScene scene, const QuadricProbe* probes, unsigned int count,
                               unsigned int* failures) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const auto& probe = probes[index];
    Hit hit{};
    const auto& shape = scene.primitives[probe.object].geometry.quadric;
    const bool found = shape.intersect(probe.ray, probe.minimum, probe.maximum, hit);
    unsigned int failed = 0;
    if (found != probe.found || (found && fabsf(hit.distance - probe.distance) > probe.tolerance)) failed |= 1;
    const auto outward = hit.front_face ? hit.normal : -hit.normal;
    if (found && (hit.front_face != probe.front || !isfinite(dot(outward, outward)) ||
                  dot(outward, normalize(probe.outward)) < 0.9999f)) failed |= 2;
    hiprtRay ray{};
    ray.origin = probe.ray.origin; ray.direction = probe.ray.direction;
    ray.minT = probe.minimum; ray.maxT = probe.maximum;
    hiprtGeomCustomTraversalClosest traversal(scene.geometry, ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const auto actual = traversal.getNextHit();
    if (actual.hasHit() != probe.found || (actual.hasHit() &&
        (actual.primID != probe.object || fabsf(actual.t - probe.distance) > probe.tolerance ||
         dot(actual.normal, normalize(probe.outward)) < 0.9999f))) failed |= 4;
    if (probe.found && probe.minimum == 0.001f) {
        Hit shaded{};
        if (!hit_custom_objects(probe.ray, shaded, scene) || shaded.object_id != probe.object ||
            shaded.material != ShaderKind::phong || shaded.albedo.z != 0.7f)
            failed |= 8;
    }
    if (failed)
        printf("Quadric probe %u: direct=%d t=%g, HIP RT=%d t=%g, flags=%u\n",
               index, found, hit.distance, actual.hasHit(), actual.t, failed);
    failures[index] = failed;
}

inline void run_quadric_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    const Material material = Phong{ConstantColor{{0.2f, 0.5f, 0.7f}}};
    description.objects = {
        {Sphere{{0, 2, 0}, 1}, material}, {Ellipsoid{{4, 2, 0}, {0.75f, 1.5f, 0.5f}}, material},
        {Cylinder{{8, 2, 0}, 1, 2}, material}, {Cone{{12, 2, 0}, 1, 2}, material},
        {Paraboloid{{16, 2, 0}, 1, 2}, material}, {Hyperboloid{{20, 2, 0}, 0.6f, 1, 2}, material},
        {Sphere{{28, 2, 0}, 0.0001f}, material}, {Sphere{{0, 10000, 0}, 1000}, material}
    };
    auto prepared = prepare_scene(description);
    GpuQuadric general;
    general.center = {24, 2, 0};
    general.diagonal = {2, 2, 2};
    general.cross = {1, 0.3f, -0.4f};
    general.linear = {0.1f, -0.2f, 0.15f};
    general = geometry::quadric::prepare(general);
    prepared.primitives.push_back({GpuGeometry{general}, prepare_material(material)});
    const auto box = primitive_bounds(GpuGeometry{general}, 0.01f);
    prepared.bounds.push_back(box[0]); prepared.bounds.push_back(box[1]);
    for (const auto& primitive : prepared.primitives)
        if (primitive.geometry.kind != GeometryKind::quadric)
            throw std::runtime_error("CPU quadratic shapes did not lower to GPU Quadric");
    const float root = (-0.41f + std::sqrt(0.41f * 0.41f + 8 * 0.835f)) / 4;
    const std::vector<QuadricProbe> probes{
        {0, {{0, 2, 3}, {0, 0, -1}}, true, 2, {0, 0, 1}},
        {0, {{0, 2, 0}, {1, 0, 0}}, true, 1, {1, 0, 0}, false},
        {0, {{1, 2, 3}, {0, 0, -1}}, true, 3, {1, 0, 0}, false},
        {0, {{0, 2, 3}, {0, 0, -2}}, true, 1, {0, 0, 1}},
        {0, {{0, 2, 3}, {0, 0, -1}}, true, 2, {0, 0, 1}, true, 2, 3},
        {0, {{0, 2, 3}, {0, 0, -1}}, false, 0, {}, true, 0.001f, 2},
        {0, {{0, 2, 3}, {0, 0, -1}}, true, 4, {0, 0, -1}, false, 2.1f, 5},
        {1, {{4, 2, 3}, {0, 0, -1}}, true, 2.5f, {0, 0, 1}},
        {1, {{4, 2, 0}, {1, 0, 0}}, true, 0.75f, {1, 0, 0}, false},
        {1, {{4, 5, 0}, {0, -1, 0}}, true, 1.5f, {0, 1, 0}},
        {1, {{5.3444272f, 2, 2.1888544f}, {-0.4472136f, 0, -0.8944272f}},
             true, 2, {0.8f, 0, 1.6f}},
        {2, {{8, 2, 3}, {0, 0, -1}}, true, 2, {0, 0, 1}},
        {2, {{8.5f, 4, 0}, {0, -1, 0}}, true, 1, {0, 1, 0}},
        {2, {{8.5f, 0, 0}, {0, 1, 0}}, true, 1, {0, -1, 0}},
        {2, {{8, 2, 0}, {0, 1, 0}}, true, 1, {0, 1, 0}, false},
        {2, {{8, 3.5f, 3}, {0, 0, -1}}, false, 0, {}},
        {2, {{9, 4, 0}, {0, -1, 0}}, true, 1, {0, 1, 0}},
        {2, {{9.01f, 4, 0}, {0, -1, 0}}, false, 0, {}},
        {2, {{8, 4, 0}, {0, -2, 0}}, true, 0.5f, {0, 1, 0}},
        {2, {{8, 4, 0}, {0, -1, 0}}, true, 3, {0, -1, 0}, false, 1.1f, 4},
        {3, {{12, 2, 3}, {0, 0, -1}}, true, 2.5f, {0, 0.5f, 1}},
        {3, {{12, 4, 0}, {0, -1, 0}}, true, 1, {0, 1, 0}},
        {3, {{12.5f, 0, 0}, {0, 1, 0}}, true, 1, {0, -1, 0}},
        {3, {{12, 2, 0}, {0, 1, 0}}, true, 1, {0, 1, 0}, false},
        {3, {{12, 3.5f, 3}, {0, 0, -1}}, false, 0, {}},
        {3, {{12, 0.5f, 3}, {0, 0, -1}}, false, 0, {}},
        {4, {{16, 2, 3}, {0, 0, -1}}, true, 2.2928932f, {0, -0.5f, 1.4142136f}},
        {4, {{16.5f, 4, 0}, {0, -1, 0}}, true, 1, {0, 1, 0}},
        {4, {{16, 0, 0}, {0, 1, 0}}, true, 1, {0, -1, 0}},
        {4, {{16, 2, 0}, {0, -1, 0}}, true, 1, {0, -1, 0}, false},
        {4, {{16, 1, 3}, {0, 0, -1}}, true, 3, {0, -1, 0}, false},
        {4, {{16, 3.5f, 3}, {0, 0, -1}}, false, 0, {}},
        {5, {{20, 2, 3}, {0, 0, -1}}, true, 2.4f, {0, 0, 1}},
        {5, {{20.7f, 4, 0}, {0, -1, 0}}, true, 1, {0, 1, 0}},
        {5, {{21, 4, 0}, {0, -1, 0}}, false, 0, {}},
        {5, {{20, 2.5f, 3}, {0, 0, -1}}, true, 2.3291796f, {0, -1, 3.72678f}},
        {5, {{20, 2, 0}, {0, -1, 0}}, true, 1, {0, -1, 0}, false},
        {6, {{28, 2, 0.01f}, {0, 0, -1}}, true, 0.0099f, {0, 0, 1}, true, 0.001f, 1, 0.0000001f},
        {7, {{0, 10000, 3000}, {0, 0, -1}}, true, 2000, {0, 0, 1}, true, 0.001f, 10000, 0.01f},
        {8, {{26, 2.25f, 0.2f}, {-1, 0, 0}}, true, 2 - root, {4 * root + 0.41f, root + 0.72f, 0.85f + 0.3f * root}}
    };
    const HiprtScene scene(device, std::move(prepared));
    auto device_probes = allocate_device<QuadricProbe>(probes.size(), "Allocating quadric probes");
    auto failures = allocate_device<unsigned int>(probes.size(), "Allocating quadric failures");
    check_hip(hipMemcpy(device_probes.get(), probes.data(), probes.size() * sizeof(QuadricProbe), hipMemcpyHostToDevice),
              "Uploading quadric probes");
    hipLaunchKernelGGL(check_quadrics, dim3(1), dim3(64), 0, 0, scene.scene(), device_probes.get(),
                       static_cast<unsigned int>(probes.size()), failures.get());
    check_hip(hipGetLastError(), "Launching quadric intersection checks");
    std::vector<unsigned int> results(probes.size());
    check_hip(hipMemcpy(results.data(), failures.get(), results.size() * sizeof(unsigned int), hipMemcpyDeviceToHost),
              "Reading quadric intersection checks");
    for (std::size_t i = 0; i < results.size(); ++i)
        if (results[i]) throw std::runtime_error("Quadric intersection probe " + std::to_string(i) + " failed");
    std::cout << "Shared GPU quadric side/cap/apex, normal, interval, scale and HIP RT checks passed.\n";
}
}
