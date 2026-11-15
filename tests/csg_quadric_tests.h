#pragma once

#include "components/geometry/csg/intersection.h"

namespace hip_demo {
__global__ void check_colored_csg(TraceScene scene, const GpuCsgLeaf* leaves,
                                  const GpuGeometry* cylinders, unsigned int* failures) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    const auto prototype = scene.prototypes[1].scene;
    const auto& object = prototype.csgs[0];
    Random random{i * 2891336453u + 741103597u};
    const Ray ray{{6 * random.next() - 3, 6 * random.next() - 3, 6 * random.next() - 3},
                  random_unit_vector(random)};
    Hit cached{}, reference{};
    const bool found = intersect_csg(ray, object, leaves, 0.001f, 20, cached);
    const bool expected = intersect_csg_uncached(ray, object, leaves, 0.001f, 20, reference);
    if (found != expected || (found && (fabsf(cached.distance - reference.distance) > 0.00005f ||
        dot(cached.normal, reference.normal) < 0.9999f || cached.csg_leaf != reference.csg_leaf)))
        atomicOr(failures, 1u);
    if (i != 0) return;
    const Ray probes[] = {{{1.2f, 0, 3}, {0, 0, -1}}, {{1.35f, 1.35f, 3}, {0, 0, -1}},
        {{}, normalize({1, 0.4f, 0.8f})}, {{}, normalize({0.4f, 1, 0.8f})},
        {{}, normalize({0.4f, 0.8f, 1})}, {{0, 0, 3}, {0, 0, -1}}};
    const float3 colors[] = {{0.8f, 0.05f, 0.05f}, {0.05f, 0.15f, 0.8f},
        {0.05f, 0.65f, 0.08f}, {0.05f, 0.65f, 0.08f}, {0.05f, 0.65f, 0.08f}};
    for (int probe = 0; probe < 6; ++probe) {
        const Ray world{probes[probe].origin + float3{4.6f, 1.5f, 0}, probes[probe].direction};
        Hit actual{}, local{};
        const bool hit = hit_objects(world, actual, scene);
        const bool direct = intersect_csg(probes[probe], object, leaves, 0.001f, 20, local);
        if (hit != (probe != 5) || direct != hit || (hit &&
            (fabsf(actual.distance - local.distance) > 0.00005f ||
             dot(actual.albedo - colors[probe], actual.albedo - colors[probe]) > 0.000001f ||
             actual.csg_leaf != local.csg_leaf || actual.object_id != 3)))
            atomicOr(failures, 2u);
        const float distance = probe == 0 ? 1.5f : 3 - sqrtf(2.25f * 2.25f - 2 * 1.35f * 1.35f);
        if (hit && probe < 2 && fabsf(actual.distance - distance) > 0.00005f)
            atomicOr(failures, 2u);
        if (hit && probe >= 2 && probe < 5 &&
            (fabsf(actual.distance - 0.93f) > 0.00005f ||
             dot(actual.geometric_normal, probes[probe].direction) >= 0))
            atomicOr(failures, 4u);
    }
    for (int axis = 0; axis < 3; ++axis) {
        const auto normal = axis_vector(static_cast<Axis>(axis), 1);
        Hit cap{};
        if (!cylinders[axis].quadric.intersect({3 * normal, -normal}, 0.001f, 20, cap) ||
            fabsf(cap.distance - 1) > 0.00001f || dot(cap.normal, normal) < 0.9999f)
            atomicOr(failures, 8u);
    }
    Hit box{};
    if (!hit_objects({{-4.6f, 1.5f, 3}, {0, 0, -1}}, box, scene) ||
        fabsf(box.distance - 1.5f) > 0.00001f || box.object_id != 0 ||
        dot(box.albedo - colors[0], box.albedo - colors[0]) > 0.000001f ||
        scene.emitters[0].kind != EmitterKind::rectangle)
        atomicOr(failures, 16u);
    const auto& shape = scene.primitives[0].geometry.box;
    if (!shape.intersect({{-4.6f, 1.5f, 0}, {1, 0, 0}}, 0.001f, 20, box) ||
        fabsf(box.distance - 1.5f) > 0.00001f || box.front_face ||
        shape.intersect({{-4.6f, 1.5f, 3}, {0, 0, -1}}, 0.001f, 1.5f, box))
        atomicOr(failures, 16u);
    if (hit_objects({{20, 2, 3}, {0, 0, -1}}, box, scene))
        atomicOr(failures, 32u);
}

inline void run_colored_csg_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    const Material red = Phong{ConstantColor{{0.8f, 0.05f, 0.05f}}};
    const Material blue = Phong{ConstantColor{{0.05f, 0.15f, 0.8f}}};
    const Material green = Phong{ConstantColor{{0.05f, 0.65f, 0.08f}}};
    const Box box{{}, {3, 3, 3}};
    const CsgNode leaf{box, CsgOperation::unite, {}};
    const CsgNode rounded{Sphere{}, CsgOperation::intersection,
                          {leaf, CsgNode{Sphere{{}, 2.25f}, CsgOperation::unite, {}}}};
    const CsgNode bores{Sphere{}, CsgOperation::unite,
        {CsgNode{Cylinder{{}, 0.62f, 6, Axis::x}, CsgOperation::unite, {}},
         CsgNode{Sphere{}, CsgOperation::unite,
            {CsgNode{Cylinder{{}, 0.62f, 6, Axis::y}, CsgOperation::unite, {}},
             CsgNode{Cylinder{{}, 0.62f, 6, Axis::z}, CsgOperation::unite, {}}}}}};
    description.objects = {{Box{{-4.6f, 1.5f, 0}, {3, 3, 3}}, red},
        {Rectangle{{0, description.settings.room.size.y - 0.01f, 0}, 10, 8},
         Emissive{ConstantColor{{20, 20, 20}}}}};
    description.prototypes = {{Csg{rounded}, red, {red, blue}},
        {Csg{CsgNode{Sphere{}, CsgOperation::difference, {rounded, bores}}},
         red, {red, blue, green, green, green}}};
    description.instances = {{0, {{0, 1.5f, 0}, {}, {1, 1, 1}}, std::nullopt},
                             {1, {{4.6f, 1.5f, 0}, {}, {1, 1, 1}}, std::nullopt}};
    description.prototypes.push_back({Csg{CsgNode{Sphere{}, CsgOperation::difference, {leaf, leaf}}},
                                      description.objects[0].material,
                                      {description.prototypes[0].csg_materials[0],
                                       description.prototypes[0].csg_materials[1]}});
    description.instances.push_back({2, {{20, 2, 0}, {}, {1, 1, 1}}, std::nullopt});
    const auto prepared = prepare_scene(description);
    const HiprtScene scene(device, prepared);
    const auto& source = prepared.prototypes[1].csg_leaves;
    auto leaves = allocate_device<GpuCsgLeaf>(source.size(), "Allocating colored CSG reference leaves");
    check_hip(hipMemcpy(leaves.get(), source.data(), source.size() * sizeof(GpuCsgLeaf), hipMemcpyHostToDevice),
              "Uploading colored CSG reference leaves");
    const GpuGeometry shapes[]{prepare_geometry(Cylinder{{}, 0.62f, 4, Axis::x}),
                               prepare_geometry(Cylinder{{}, 0.62f, 4, Axis::y}),
                               prepare_geometry(Cylinder{{}, 0.62f, 4, Axis::z})};
    auto cylinders = allocate_device<GpuGeometry>(3, "Allocating axis cylinder probes");
    check_hip(hipMemcpy(cylinders.get(), shapes, sizeof(shapes), hipMemcpyHostToDevice), "Uploading axis cylinders");
    auto failures = allocate_device<unsigned int>(1, "Allocating colored CSG failures");
    check_hip(hipMemset(failures.get(), 0, sizeof(unsigned int)), "Clearing colored CSG failures");
    hipLaunchKernelGGL(check_colored_csg, dim3(16), dim3(256), 0, 0, scene.scene(), leaves.get(),
                       cylinders.get(), failures.get());
    check_hip(hipGetLastError(), "Launching colored CSG checks");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failures.get(), sizeof(result), hipMemcpyDeviceToHost), "Reading colored CSG checks");
    if (result) throw std::runtime_error("Colored CSG face/corner/hole, axis or boundary checks failed (flags " +
                                         std::to_string(result) + ")");
    std::cout << "Analytic boxes, X/Y/Z cylinders and colored CSG faces/corners/cut walls passed.\n";
}

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
    const bool expect_cache = test.object != 6;
    const bool failed = usable != expect_cache || found != test.found || (usable && cached_found != found) ||
        reference_found != found || actual.hasHit() != found || (found &&
        (fabsf(direct.distance - test.distance) > 0.00005f ||
         (usable && fabsf(cached.distance - direct.distance) > 0.00005f) ||
         fabsf(reference.distance - direct.distance) > 0.00005f ||
         fabsf(actual.t - direct.distance) > 0.00005f ||
         dot(outward, normalize(test.outward)) < 0.9999f ||
         (usable && dot(cached.normal, direct.normal) < 0.9999f) ||
         dot(reference.normal, direct.normal) < 0.9999f ||
         dot(actual.normal, normalize(test.outward)) < 0.9999f ||
         actual.primID != test.object));
    if (failed)
        printf("CSG quadric probe %u: direct=%d t=%g normal=(%g,%g,%g), cached=%d t=%g, reference=%d t=%g, HIP=%d t=%g\n",
               i, found, direct.distance, outward.x, outward.y, outward.z,
               cached_found, cached.distance, reference_found, reference.distance, actual.hasHit(), actual.t);
    failures[i] = failed;
}

__global__ void check_csg_quadric_cache(TraceScene scene, const GpuCsgLeaf* leaves, unsigned int* failures) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int index = i % 6;
    Random random{i * 2891336453u + 741103597u};
    const Ray ray{{10 * float(index) + 4 * random.next() - 2,
                   4 * random.next(), 4 * random.next() - 2}, random_unit_vector(random)};
    const float minimum = 2 * random.next(), maximum = minimum + 5 * random.next();
    Hit actual{}, reference{};
    const auto& object = scene.csgs[index];
    const bool found = intersect_csg(ray, object, leaves, minimum, maximum, actual);
    const bool expected = intersect_csg_uncached(ray, object, leaves, minimum, maximum, reference);
    if (found != expected || (found && (fabsf(actual.distance - reference.distance) > 0.00005f ||
                                         dot(actual.normal, reference.normal) < 0.9999f)))
        atomicOr(failures, 1u);
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
    auto oversized = leaf(Sphere{{60, 2, 0}, 1});
    for (int i = 1; i < 65; ++i)
        oversized = CsgNode{Sphere{}, CsgOperation::unite,
                            {std::move(oversized), leaf(Sphere{{60, 2, 0}, 1})}};
    description.objects.push_back({Csg{std::move(oversized)}, material});
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
        {5, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, false, 0, {}},
        {6, {{0, 2, 3}, {0, 0, -1}}, .001f, 8, true, 2, {0, 0, 1}}
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
    check_hip(hipMemset(failures.get(), 0, sizeof(unsigned int)), "Clearing randomized CSG quadric failures");
    hipLaunchKernelGGL(check_csg_quadric_cache, dim3(16), dim3(256), 0, 0,
                       scene.scene(), leaves.get(), failures.get());
    check_hip(hipGetLastError(), "Launching randomized CSG quadric comparisons");
    unsigned int failed = 0;
    check_hip(hipMemcpy(&failed, failures.get(), sizeof(failed), hipMemcpyDeviceToHost),
              "Reading randomized CSG quadric comparisons");
    if (failed) throw std::runtime_error("Randomized CSG quadric cache/reference comparison failed");
    std::cout << "CSG quadric capped boundaries, parity, normals, intervals and HIP RT checks passed.\n";
}
}
