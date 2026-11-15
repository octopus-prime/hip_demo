#pragma once

#include "components/geometry/csg/preparation.h"
#include "components/geometry/csg/builder.h"
#include "components/geometry/csg/intersection.h"

namespace hip_demo {

__global__ void check_csg_text(TraceScene scene, const GpuCsg* objects, const GpuCsgLeaf* leaves,
                               TextGeometryView text, unsigned int* failure) {
    struct Probe {
        unsigned int object;
        Ray ray;
        float minimum, maximum;
        bool found;
        float distance;
        float3 outward;
        bool cached = true;
    };
    const Probe probes[]{
        {0, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 1, {-1, 0, 0}},
        {0, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 2.1f, 6, true, 4, {1, 0, 0}},
        {0, {{1.5f, 1.5f, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 1.5f, {1, 0, 0}},
        {1, {{0.5f, 1.5f, 2}, {0, 0, -1}}, 0.001f, 6, true, 1.8f, {0, 0, 1}},
        {1, {{0.5f, 1.5f, 0.1f}, {0, 0, 1}}, 0.001f, 6, true, 0.1f, {0, 0, 1}},
        {1, {{-1, 1.5f, 0.4f}, {1, 0, 0}}, 1.1f, 6, true, 1.3f, {1, 0, 0}},
        {1, {{1.5f, 1.5f, 2}, {0, 0, -1}}, 0.001f, 6, false, 0, {}},
        {2, {{-1, 0.5f, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 1.5f, {-1, 0, 0}},
        {2, {{-1, 0.5f, 0.25f}, {2, 0, 0}}, 0.001f, 6, true, 0.75f, {-1, 0, 0}},
        {3, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 0.001f, 6, false, 0, {}},
        {4, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 2.1f, 6, true, 2.5f, {1, 0, 0}},
        {4, {{1.25f, 1.5f, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 0.25f, {1, 0, 0}},
        {5, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 1.25f, {-1, 0, 0}},
        {5, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 1.3f, 6, true, 1.4f, {1, 0, 0}},
        {5, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 1.5f, 6, true, 1.6f, {-1, 0, 0}},
        {6, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 10.1f, 13, true, 11, {-1, 0, 0}},
        {6, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 11.1f, 13, true, 12, {1, 0, 0}},
        {6, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 1.5f, 2, true, 1.5f, {0, 0, 1}},
        {6, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 0.001f, 1.5f, false, 0, {}},
        {6, {{-1, 3, 0.25f}, {1, 0, 0}}, 0.001f, 13, false, 0, {}},
        {6, {{-1, 0.5f, 0}, {1, 0, 0}}, 0.001f, 13, false, 0, {}},
        {7, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 0.001f, 6, false, 0, {}},
        {8, {{0.5f, 1.5f, 3}, {0, 0, -1}}, 1.2f, 6, true, 2.5f, {0, 0, -1}},
        {8, {{0.5f, 1.5f, 0.25f}, {0, 0, 1}}, 0.001f, 6, true, 0.25f, {0, 0, -1}},
        {8, {{1.5f, 1.5f, 0.25f}, {-1, 0, 0}}, 0.001f, 6, true, 0.5f, {-1, 0, 0}},
        {8, {{1.5f, 1.5f, 0.25f}, {0, 0, 1}}, 0.001f, 6, true, 2, {0, 0, 1}},
        {9, {{-1, 1, 0.25f}, {1, 0, 0}}, 0.001f, 6, true, 2, {-1, 0, 0}},
        {10, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 0.001f, 81, true, 1, {-1, 0, 0}, false},
        {10, {{-1, 1.5f, 0.25f}, {1, 0, 0}}, 76.1f, 81, true, 77, {-1, 0, 0}},
        {11, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 0.001f, 6, true, 1.5f, {0, 0, 1}, false},
        {12, {{0.5f, 0.5f, 2}, {0, 0, -1}}, 0.001f, 6, true, 1.5f, {0, 0, 1}, false},
    };
    unsigned int failed = 0;
    for (unsigned int i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const auto& test = probes[i];
        Ray ray = test.ray;
        ray.origin.x += 20 * test.object;
        Hit hit{};
        const bool found = intersect_csg(ray, objects[test.object], leaves, test.minimum, test.maximum, hit, text);
        Hit cached{}, reference{};
        bool usable = false;
        const bool cached_found = intersect_csg_cached(
            ray, objects[test.object], leaves, test.minimum, test.maximum, cached, text, usable);
        const bool reference_found = intersect_csg_uncached(
            ray, objects[test.object], leaves, test.minimum, test.maximum, reference, text);
        hiprtRay traversal_ray{};
        traversal_ray.origin = ray.origin; traversal_ray.direction = ray.direction;
        traversal_ray.minT = test.minimum; traversal_ray.maxT = test.maximum;
        hiprtGeomCustomTraversalClosest traversal(
            scene.geometry, traversal_ray, hiprtTraversalHintDefault, nullptr, scene.functions);
        const auto actual = traversal.getNextHit();
        const float3 outward = hit.front_face ? hit.normal : -hit.normal;
        if (usable != test.cached || reference_found != test.found ||
            (usable && (cached_found != found || (found &&
                (fabsf(cached.distance - reference.distance) > 0.00001f ||
                 dot(cached.normal, reference.normal) < 0.99999f ||
                 cached.front_face != reference.front_face)))) ||
            found != test.found || actual.hasHit() != test.found || (found &&
            (fabsf(hit.distance - test.distance) > 0.00005f || dot(outward, test.outward) < 0.9999f ||
             hit.front_face != (dot(ray.direction, test.outward) < 0) ||
             fabsf(actual.t - test.distance) > 0.00005f || actual.primID != test.object ||
             dot(actual.normal, test.outward) < 0.9999f))) {
            printf("Text CSG probe %u: direct=%d t=%g normal=(%g,%g,%g), HIP RT=%d t=%g\n",
                   i, found, hit.distance, outward.x, outward.y, outward.z, actual.hasHit(), actual.t);
            failed |= 1u << i;
        }
    }
    Hit cut{};
    if (!hit_custom_objects({{20.5f, 1.5f, 2}, {0, 0, -1}}, cut, scene) ||
        cut.object_id != 1 || cut.material != ShaderKind::phong || cut.albedo.z != 0.9f)
        failed |= 1u << 31;
    Hit cancelled{};
    bool usable = false;
    if (!intersect_csg_cached({{259, 1.5f, 0.25f}, {1, 0, 0}}, objects[13], leaves,
                              0.001f, 6, cancelled, text, usable) || !usable ||
        fabsf(cancelled.distance - 1) > 0.00001f ||
        dot(cancelled.normal, {-0.8f, -0.6f, 0}) < 0.99999f) {
        printf("Cancelled text must not supply the coincident sphere normal\n");
        failed |= 1u << 31;
    }
    *failure = failed;
}

__global__ void check_csg_cached_rays(const GpuCsg* objects, const GpuCsgLeaf* leaves,
                                     TextGeometryView text, unsigned int* failure) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= 4096) return;
    const auto object = index % 10;
    unsigned int seed = index + 1;
    const auto next = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) * 0x1p-24f;
    };
    const Ray ray{{20.0f * object - 1 + 5 * next(), -1 + 5 * next(), -2 + 5 * next()},
                  {2 * next() - 1, 2 * next() - 1, 2 * next() - 1}};
    const float minimum = index % 3 == 0 ? 0.75f : 0.001f;
    Hit cached{}, reference{};
    bool usable = false;
    const bool found = intersect_csg_cached(ray, objects[object], leaves, minimum, 12, cached, text, usable);
    const bool expected = intersect_csg_uncached(ray, objects[object], leaves, minimum, 12, reference, text);
    if (!usable || found != expected || (found &&
        (fabsf(cached.distance - reference.distance) > 0.00002f ||
         cached.front_face != reference.front_face || dot(cached.normal, reference.normal) < 0.99999f))) {
        printf("Cached CSG ray %u object %u: usable=%d cached=%d t=%g reference=%d t=%g\n",
               index, object, usable, found, cached.distance, expected, reference.distance);
        atomicOr(failure, 1u << 31);
    }
}

inline void run_csg_text_tests(int device) {
    using scene_ast::CsgNode;
    const auto text_node = [](double x, const std::string& value = "O") {
        return CsgNode{0, scene_ast::Text{0, "synthetic.ttf", value, 3, 0.5, {x, 0, 0}, {}},
                       CsgOperation::unite, {}};
    };
    const auto sphere_node = [](double x, double y, double z, double radius) {
        return CsgNode{0, scene_ast::Sphere{0, {x, y, z}, radius, {}}, CsgOperation::unite, {}};
    };
    const auto branch = [](CsgOperation operation, CsgNode left, CsgNode right) {
        return CsgNode{0, scene_ast::Sphere{}, operation, {std::move(left), std::move(right)}};
    };
    PreparedScene prepared;
    prepared.settings = prepare_settings(load_scene_description(builtin_scene_path(false)).settings);
    std::vector<ExtrudedText> texts;
    const GpuMaterial material{{0.1f, 0.2f, 0.9f}, ShaderKind::phong, 0};
    const SceneDiagnostics errors{"", "synthetic-text-csg"};
    const auto append = [&](CsgNode node) {
        append_csg(prepared, hip_demo::geometry::csg::build(scene_ast::Csg{0, {}, std::move(node)}, SceneValues{errors}), material, texts);
    };
    append(branch(CsgOperation::unite, text_node(0), sphere_node(1.5, 1.5, 0.25, 0.6)));
    append(branch(CsgOperation::difference, text_node(20), sphere_node(20.5, 1.5, 0.4, 0.2)));
    append(branch(CsgOperation::intersection, text_node(40), sphere_node(41.5, 0.5, 0.25, 1)));
    append(branch(CsgOperation::difference, text_node(60), text_node(60)));
    append(text_node(80, "overlap"));
    CsgNode torus{0, scene_ast::Torus{0, {101.5, 1.5, 0.25}, 1, 0.25, 90, {}},
                   CsgOperation::unite, {}};
    append(branch(CsgOperation::difference,
        branch(CsgOperation::intersection, std::move(torus), text_node(100)),
        sphere_node(100.5, 1.5, 0.25, 0.1)));
    append(text_node(120, "three"));
    append(text_node(140, " "));
    append(branch(CsgOperation::difference, sphere_node(161.5, 1.5, 0.25, 2), text_node(160)));
    append(text_node(180, "tangent"));
    append(text_node(200, "overflow"));
    append(text_node(220, "glyph-limit"));
    auto many_leaves = text_node(240);
    for (int i = 1; i < 65; ++i)
        many_leaves = branch(CsgOperation::unite, std::move(many_leaves), text_node(240));
    append(std::move(many_leaves));
    append(branch(CsgOperation::unite, sphere_node(260.5, 1.875, 0.25, 0.625),
        branch(CsgOperation::difference, text_node(260), text_node(260))));

    prepared.text.curves = {
        {{0, 3, 0, 0}, {0, 0, 0, 0}}, {{3, 0, 0, 0}, {0, 3, 0, 0}},
        {{3, -3, 0, 0}, {3, 0, 0, 0}}, {{0, 0, 0, 0}, {3, -3, 0, 0}},
        {{1, 0, 0, 0}, {1, 1, 0, 0}}, {{1, 1, 0, 0}, {2, 0, 0, 0}},
        {{2, 0, 0, 0}, {2, -1, 0, 0}}, {{2, -1, 0, 0}, {1, 0, 0, 0}},
        {{0, 2, 0, 0}, {0, 4, -4, 0}}, {{2, -2, 0, 0}, {0, 0, 0, 0}}};
    for (unsigned int i = 0; i < texts.size(); ++i) {
        const auto& source = texts[i];
        const auto first = static_cast<std::uint32_t>(prepared.text.glyphs.size());
        const unsigned int count = source.value == " " ? 0 : source.value == "overflow" ? 20 :
            source.value == "glyph-limit" ? 33 : source.value == "three" ? 3 :
            source.value == "overlap" || source.value == "tangent" ? 2 : 1;
        for (unsigned int glyph = 0; glyph < count; ++glyph) {
            TextGlyph outline;
            outline.position = source.position;
            outline.position.x += glyph * (source.value == "glyph-limit" ? 0.0f :
                source.value == "overlap" ? 0.5f : 4.0f);
            outline.minimum = outline.position;
            outline.maximum = {outline.position.x + 3, 3, 0.5f};
            outline.depth = 0.5f;
            outline.curve_count = 8;
            outline.even_odd = i % 2 != 0;
            if (source.value == "tangent") {
                outline.position = {source.position.x + glyph, 0, 0};
                outline.minimum = outline.position;
                outline.maximum = {outline.position.x + (glyph == 0 ? 2 : 3),
                                   glyph == 0 ? 1.0f : 3.0f, 0.5f};
                outline.first_curve = glyph == 0 ? 8 : 0;
                outline.curve_count = glyph == 0 ? 2 : 4;
                outline.normal_sign = glyph == 0 ? -1.0f : 1.0f;
            }
            prepared.text.glyphs.push_back(outline);
        }
        for (auto& leaf : prepared.csg_leaves) {
            if (leaf.text && leaf.text_index == i) {
                leaf.first_glyph = first;
                leaf.glyph_count = count;
            }
        }
    }
    for (unsigned int i = 0; i < prepared.csgs.size(); ++i) {
        const float x = 20.0f * i;
        prepared.bounds.push_back({x - 0.6f, -0.6f, -2});
        prepared.bounds.push_back({x + (i == 10 ? 80 : 12), 3.6f, 2.5f});
    }
    const auto upload = []<typename T>(const std::vector<T>& data) {
        auto buffer = allocate_device<T>(data.size(), "Allocating text CSG test data");
        check_hip(hipMemcpy(buffer.get(), data.data(), data.size() * sizeof(T), hipMemcpyHostToDevice),
                  "Uploading text CSG test data");
        return buffer;
    };
    auto objects = upload(prepared.csgs);
    auto leaves = upload(prepared.csg_leaves);
    auto glyphs = upload(prepared.text.glyphs);
    auto curves = upload(prepared.text.curves);
    const HiprtScene scene(device, std::move(prepared));
    auto failure = allocate_device<unsigned int>(1, "Allocating text CSG test result");
    hipLaunchKernelGGL(check_csg_text, dim3(1), dim3(1), 0, 0, scene.scene(), objects.get(), leaves.get(),
                       (TextGeometryView{glyphs.get(), curves.get()}), failure.get());
    check_hip(hipGetLastError(), "Launching text CSG tests");
    hipLaunchKernelGGL(check_csg_cached_rays, dim3(64), dim3(64), 0, 0, objects.get(), leaves.get(),
                       (TextGeometryView{glyphs.get(), curves.get()}), failure.get());
    check_hip(hipGetLastError(), "Launching cached/uncached text CSG comparisons");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost), "Reading text CSG tests");
    if (result != 0) throw std::runtime_error("Text CSG tests failed (flags " + std::to_string(result) + ")");
    std::cout << "Analytic text CSG Boolean, counter, overlap, interval, cache/overflow and HIP RT checks passed.\n";
}

}
