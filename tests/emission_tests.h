#pragma once

#include "gpu/scene.h"
#include "rendering/integrator.h"
#include <filesystem>
#include <iostream>

namespace hip_demo {

__global__ void check_emitter_samples(TraceScene scene, unsigned int* counts, unsigned int* failure) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    Random random{index * 2891336453u + 741103597u};
    const auto light = sample_light(random, scene);
    unsigned int failed = 0;
    atomicAdd(counts + light.object_id, 1u);
    Hit hit{};
    if (!hit_objects({light.point + 0.05f * light.normal, -light.normal}, hit, scene) ||
        hit.object_id != light.object_id || !hit.front_face || !hit.sampled_emitter ||
        fabsf(hit.distance - 0.05f) > 0.001f ||
        dot(hit.emission - light.radiance, hit.emission - light.radiance) > 0.000001f)
        failed |= 1;
    if (light.object_id == 0) {
        const auto& torus = scene.primitives[0].geometry.torus;
        const auto local = rotate_x(light.point - vector3(torus.center), -torus.tilt_sine, torus.tilt_cosine);
        const float radial = sqrtf(local.x * local.x + local.z * local.z);
        const float expected = 1 / (3 * 4 * pi * pi * torus.minor_radius * radial);
        if (fabsf(torus_distance(local, {}, torus.major_radius, torus.minor_radius)) > 0.0001f ||
            fabsf(light.area_pdf / expected - 1) > 0.0001f || light.radiance.y != 3)
            failed |= 2;
    } else if (light.object_id == 1) {
        atomicAdd(counts + 3 + light.triangle, 1u);
        const auto& emitter = scene.emitters[1];
        if (fabsf(light.area_pdf * static_cast<float>(emitter.area) * 3 - 1) > 0.0001f ||
            fabsf(light.point.y - 3 - 0.5f * (light.point.z + 1)) > 0.0001f ||
            light.point.x < -5 || light.point.x > -1 ||
            light.normal.y > -0.89f || light.radiance.x != 12)
            failed |= 4;
    } else if (light.object_id == 2) {
        if (light.point.y != 8 || fabsf(light.point.x) > 1 || fabsf(light.point.z) > 0.5f ||
            fabsf(light.area_pdf - 1.0f / 6) > 0.0001f || light.normal.y != -1 ||
            light.radiance.x != 7)
            failed |= 8;
    } else failed |= 16;
    if (fabsf(dot(light.normal, light.normal) - 1) > 0.0001f) failed |= 32;
    if (index == 0) {
        const auto front = trace({{0, 7, 0}, {0, 1, 0}}, random, scene);
        const auto back = trace({{0, 9, 0}, {0, -1, 0}}, random, scene);
        if (front.x != 7 || front.y != 5 || front.z != 3 || dot(back, back) != 0)
            failed |= 64;
    }
    atomicOr(failure, failed);
}

__global__ void check_emission_transport(TraceScene scene, float3* results) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    Random random{index * 2891336453u + 741103597u};
    results[index] = trace({{0, 1, 0}, {0, -1, 0}}, random, scene);
}

inline void run_emission_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    description.objects.clear();
    description.settings.room.size = {40, 40, 40};
    for (auto& texture : description.settings.room.textures) texture = ConstantColor{{1, 1, 1}};
    const auto emit = [](SceneVector radiance) { return Emissive{ConstantColor{radiance}}; };
    const auto asset = std::filesystem::path(builtin_scene_path(false)).parent_path() / "light_panel.ply";
    Mesh panel;
    panel.file = asset.string();
    panel.fit = {4, 2, 2};
    panel.position = {-3, 3, 0};
    description.objects = {
        {Torus{{3, 5, 0}, 1, 0.25f, 0.6f, 0.8f}, emit({2, 3, 4})},
        {panel, emit({12, 8, 3})},
        {hip_demo::Rectangle{{0, 8, 0}, 2, 1}, emit({7, 5, 3})}
    };
    const auto prepared = prepare_scene(description);
    if (prepared.emitters.size() != 3 || prepared.emitter_triangles.size() != 2 ||
        std::abs(prepared.emitters[1].area - 3.5 * std::sqrt(5.0)) > 0.000001 ||
        std::abs(prepared.emitter_triangles[0].cumulative_area / prepared.emitters[1].area - 4.0 / 7) > 0.000001)
        throw std::runtime_error("Emitter distributions must use final transformed triangle areas");
    constexpr unsigned int count = 8192;
    auto counts = allocate_device<unsigned int>(5, "Allocating emitter sampling counts");
    auto failure = allocate_device<unsigned int>(1, "Allocating emitter sampling failures");
    check_hip(hipMemset(counts.get(), 0, 5 * sizeof(unsigned int)), "Clearing emitter sampling counts");
    check_hip(hipMemset(failure.get(), 0, sizeof(unsigned int)), "Clearing emitter sampling failures");
    {
        const HiprtScene scene(device, prepared);
        hipLaunchKernelGGL(check_emitter_samples, dim3(count / 256), dim3(256), 0, 0,
                           scene.scene(), counts.get(), failure.get());
        check_hip(hipGetLastError(), "Launching emitter sampling checks");
        unsigned int failed = 0, frequencies[5]{};
        check_hip(hipMemcpy(&failed, failure.get(), sizeof(failed), hipMemcpyDeviceToHost),
                  "Reading emitter sampling failures");
        check_hip(hipMemcpy(frequencies, counts.get(), sizeof(frequencies), hipMemcpyDeviceToHost),
                  "Reading emitter sampling counts");
        if (failed) throw std::runtime_error("Emitter sampling checks failed (flags " + std::to_string(failed) + ")");
        for (unsigned int i = 0; i < 3; ++i)
            if (std::abs(double(frequencies[i]) / count - 1.0 / 3) > 0.025)
                throw std::runtime_error("Emitter selection is not uniform");
        if (std::abs(double(frequencies[3]) / frequencies[1] - 4.0 / 7) > 0.035)
            throw std::runtime_error("Mesh triangle selection is not proportional to area");
    }
    auto degenerate = prepared;
    for (unsigned int i = 0; i < 6; ++i) degenerate.vertices[i] = {};
    bool rejected = false;
    try { prepare_emitters(degenerate, 2); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("positive, usable surface area") == std::string::npos) throw;
        rejected = true;
    }
    if (!rejected) throw std::runtime_error("Degenerate emissive mesh was accepted");

    auto results = allocate_device<float3>(count, "Allocating emission transport results");
    const auto transport = [&](PreparedScene source, const char* reason) {
        const HiprtScene scene(device, std::move(source));
        hipLaunchKernelGGL(check_emission_transport, dim3(count / 256), dim3(256), 0, 0,
                           scene.scene(), results.get());
        check_hip(hipGetLastError(), "Launching emission transport checks");
        std::vector<float3> samples(count);
        check_hip(hipMemcpy(samples.data(), results.get(), count * sizeof(float3), hipMemcpyDeviceToHost),
                  "Reading emission transport checks");
        double x = 0, y = 0, z = 0;
        for (const auto sample : samples) {
            if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || !std::isfinite(sample.z))
                throw std::runtime_error("Emission transport produced nonfinite radiance");
            x += sample.x; y += sample.y; z += sample.z;
        }
        if (std::abs(x / count / 2 - 1) > 0.05 || std::abs(y / count / 3 - 1) > 0.05 ||
            std::abs(z / count / 4 - 1) > 0.05)
            throw std::runtime_error(reason);
    };
    CsgNode shell{Sphere{{0, 0, 0}, 6}, CsgOperation::difference,
                  {CsgNode{Sphere{{0, 0, 0}, 6}}, CsgNode{Sphere{{0, 0, 0}, 5}}}};
    description.objects = {{Csg{shell}, emit({2, 3, 4})},
                           {hip_demo::Rectangle{{0, 8, 0}, 2, 2}, emit({20, 20, 20})}};
    transport(prepare_scene(description), "Unsampled CSG emission was lost after a diffuse bounce");
    description.objects.resize(1);
    transport(prepare_scene(description), "Emission-only scenes without direct-light samplers lost illumination");
    description.objects.clear();
    auto box = prepare_scene(description);
    const auto face = [&](float3 a, float3 b, float3 c, float3 d) {
        append_triangle(box.vertices, a, b, c);
        append_triangle(box.vertices, a, c, d);
    };
    face({-5, 5, -5}, {5, 5, -5}, {5, 5, 5}, {-5, 5, 5});
    face({-5, -0.1f, -5}, {-5, 5, -5}, {-5, 5, 5}, {-5, -0.1f, 5});
    face({5, -0.1f, 5}, {5, 5, 5}, {5, 5, -5}, {5, -0.1f, -5});
    face({5, -0.1f, -5}, {5, 5, -5}, {-5, 5, -5}, {-5, -0.1f, -5});
    face({-5, -0.1f, 5}, {-5, 5, 5}, {5, 5, 5}, {5, -0.1f, 5});
    box.triangle_materials.resize(box.vertices.size() / 3, {prepare_material(emit({2, 3, 4})), 0});
    prepare_emitters(box);
    transport(std::move(box), "Direct lighting and BSDF hits double-counted the mesh emitter");
    std::cout << "Emissive objects, transformed mesh sampling, multiple lights and emission transport checks passed.\n";
}
}
