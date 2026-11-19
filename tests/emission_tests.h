#pragma once

#include "gpu/scene.h"
#include "rendering/integrator.h"
#include <filesystem>
#include <iostream>

namespace hip_demo {

struct OcclusionProbe {
    Ray ray;
    float maximum;
    bool blocked;
};

template<bool Unified>
__global__ void check_occlusion(TraceScene scene, const OcclusionProbe* probes, unsigned int count,
                                unsigned int* failure) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) {
        const auto probe = probes[i];
        Hit reference{};
        const bool blocked = hit_scene<true, Unified>(probe.ray, reference, scene) && reference.distance < probe.maximum;
        if (blocked != probe.blocked || occluded<true, Unified>(probe.ray, probe.maximum, scene) != probe.blocked)
            atomicOr(failure, 1u);
    }
    Random random{i * 2891336453u + 741103597u};
    const Ray ray{{12 * random.next() - 6, 4 * random.next(), 6 * random.next() - 3},
                  random_unit_vector(random)};
    const float maximum = 8 * random.next();
    Hit reference{};
    const bool blocked = hit_scene<true, Unified>(ray, reference, scene) && reference.distance < maximum;
    if (occluded<true, Unified>(ray, maximum, scene) != blocked) atomicOr(failure, 2u);
}

inline void run_occlusion_tests(int device, SceneDescription description) {
    const Material glass = Glass{ConstantColor{{1, 1, 1}}};
    CsgNode cut{Sphere{}, CsgOperation::difference,
                {CsgNode{Cylinder{{0, 2, 0}, 1, 2}}, CsgNode{Cylinder{{0, 2, 0}, 0.4f, 4}}}};
    description.objects = {{Sphere{{-4, 2, 0}, 1}, glass}, {Csg{cut}, glass}};
    auto prepared = prepare_scene(description);
    append_triangle(prepared.vertices, {3, 1, 0}, {5, 1, 0}, {4, 3, 0});
    prepared.triangle_materials.resize(1, {prepare_material(glass), 2});
    const std::vector<OcclusionProbe> probes{
        {{{-4, 2, 3}, {0, 0, -1}}, 2, false},
        {{{-4, 2, 3}, {0, 0, -1}}, 2.01f, true},
        {{{0, 2, 3}, {0, 0, -1}}, 2, false},
        {{{0, 2, 3}, {0, 0, -1}}, 2.01f, true},
        {{{0, 5, 0}, {0, -1, 0}}, 5, false},
        {{{0.7f, 5, 0}, {0, -1, 0}}, 2.01f, true},
        {{{4, 2, 3}, {0, 0, -1}}, 3, false},
        {{{4, 2, 3}, {0, 0, -1}}, nextafterf(3, 4), true},
        {{{4, 2, 0.0005f}, {0, 0, -1}}, 1, false},
        {{{10, 1, 0}, {0, -1, 0}}, 1, false},
        {{{10, 1, 0}, {0, -1, 0}}, 1.01f, true},
        {{{10, 1, 0}, {0, -1, 0}}, 0.001f, false},
        {{{10, 1, 0}, {0, 0, 1}}, 30, false}
    };
    auto input = allocate_device<OcclusionProbe>(probes.size(), "Allocating occlusion probes");
    auto failure = allocate_device<unsigned int>(1, "Allocating occlusion failures");
    check_hip(hipMemcpy(input.get(), probes.data(), probes.size() * sizeof(OcclusionProbe), hipMemcpyHostToDevice),
              "Uploading occlusion probes");
    for (const bool unified : {false, true}) {
        const HiprtScene scene(device, prepared, nullptr, 0, unified);
        check_hip(hipMemset(failure.get(), 0, sizeof(unsigned int)), "Clearing occlusion failures");
        const auto kernel = unified ? check_occlusion<true> : check_occlusion<false>;
        hipLaunchKernelGGL(kernel, dim3(16), dim3(256), 0, 0, scene.scene(), input.get(),
                           static_cast<unsigned int>(probes.size()), failure.get());
        check_hip(hipGetLastError(), "Launching occlusion checks");
        unsigned int result = 0;
        check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost), "Reading occlusion checks");
        if (result) throw std::runtime_error("Bounded occlusion checks failed (flags " + std::to_string(result) + ")");
    }
}

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
    const double expected_density = double(light.area_pdf) * 0.05 * 0.05;
    if (std::abs(light_pdf({light.point + 0.05f * light.normal, -light.normal}, hit, scene) /
                 expected_density - 1) > 0.002)
        failed |= 128;
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

__global__ void check_mis_bsdf(unsigned int* failure) {
    unsigned int failed = 0;
    Hit hit{};
    hit.normal = hit.geometric_normal = {0, 1, 0};
    hit.albedo = {0.2f, 0.5f, 0.8f};
    hit.material = ShaderKind::phong;
    const Ray incoming{{0, 1, 0}, {0, -1, 0}};
    Random random{741103597u};
    constexpr int count = 8192;
    double cosine_sum = 0, pdf_integral = 0;
    float3 energy{};
    for (int i = 0; i < count; ++i) {
        Ray outgoing = incoming;
        float3 throughput{1, 1, 1};
        if (!scatter(outgoing, hit, random, throughput)) {
            failed |= 1;
            continue;
        }
        const float cosine = dot(hit.normal, outgoing.direction);
        const float density = scattering_pdf(incoming, hit, outgoing.direction);
        const auto expected = evaluate_scattering(incoming, hit, outgoing.direction, cosine) / density;
        if (!(density > 0) || dot(expected - throughput, expected - throughput) > 1e-8f ||
            fabsf(dot(outgoing.direction, outgoing.direction) - 1) > 0.0001f)
            failed |= 2;
        energy = energy + throughput;
        cosine_sum += cosine;
        const float c = (i + 0.5f) / count;
        pdf_integral += scattering_pdf(incoming, hit, {sqrtf(1 - c * c), c, 0}) * (2 * pi / count);
    }
    const float specular_energy = phong_specular_reflectance * (phong_exponent + 1) / (phong_exponent + 2);
    const float3 expected_energy = 0.5f * (hit.albedo + float3{specular_energy, specular_energy, specular_energy});
    const auto error = energy / count - expected_energy;
    if (dot(error, error) > 0.0004f || fabs(cosine_sum / count -
        (1.0 / 3 + 0.5 * (phong_exponent + 1) / (phong_exponent + 2))) > 0.015 ||
        fabs(pdf_integral - 1) > 0.0002)
        failed |= 4;
    const float a = power_heuristic(1e30, 2e30), b = power_heuristic(2e30, 1e30);
    if (!isfinite(a) || fabsf(a - 0.2f) > 0.000001f || fabsf(a + b - 1) > 0.000001f)
        failed |= 8;
    *failure = failed;
}

struct NearFieldSample {
    float mis;
    float nee;
};

__global__ void check_terminal_bounce_mis(TraceScene scene, unsigned int* failure) {
    Random random{threadIdx.x * 2891336453u + 741103597u};
    Ray ray{{-0.5f, 0.6f, 0}, normalize({1, 1, 0})};
    const auto actual = trace(ray, random, scene);
    random = {threadIdx.x * 2891336453u + 741103597u};
    float3 throughput{1, 1, 1};
    for (int bounce = 0; bounce < maximum_bounces - 1; ++bounce) {
        Hit hit{};
        if (!hit_scene(ray, hit, scene) || hit.material != ShaderKind::metal ||
            !scatter(ray, hit, random, throughput)) {
            atomicOr(failure, 1u);
            return;
        }
        ray.origin = hit.point + hit.geometric_normal * 0.002f;
    }
    Hit floor{};
    if (!hit_scene(ray, floor, scene) || floor.material != ShaderKind::diffuse) {
        atomicOr(failure, 2u);
        return;
    }
    const auto light = sample_light(random, scene);
    const auto offset = light.point - (floor.point + floor.geometric_normal * 0.002f);
    const float squared = dot(offset, offset);
    const auto direction = offset / sqrtf(squared);
    const auto expected = throughput * evaluate_scattering(ray, floor, direction, dot(floor.normal, direction)) *
        light.radiance * (dot(light.normal, -direction) / (light.area_pdf * squared));
    if (!(expected.x > 0) || dot(actual - expected, actual - expected) > 0.000001f * dot(expected, expected))
        atomicOr(failure, 4u);
}

inline void run_terminal_bounce_mis_test(int device, SceneDescription description) {
    description.objects.clear();
    const auto white = ConstantColor{{1, 1, 1}};
    for (int i = 0; i < maximum_bounces; ++i) {
        const Material material = i + 1 == maximum_bounces ? Material{Diffuse{white}} : Material{Metal{white, 0}};
        description.objects.push_back({hip_demo::Rectangle{{float(i), 0.1f + float((i + 1) % 2), 0}, 0.6f, 2}, material});
    }
    auto prepared = prepare_scene(description);
    const float x = maximum_bounces - 0.5f;
    append_triangle(prepared.vertices, {x, 0.15f, -1}, {x, 0.15f, 1}, {x, 1.15f, 1});
    append_triangle(prepared.vertices, {x, 0.15f, -1}, {x, 1.15f, 1}, {x, 1.15f, -1});
    prepared.triangle_materials.resize(prepared.vertices.size() / 3,
                                      {prepare_material(Emissive{ConstantColor{{2, 3, 4}}}), maximum_bounces});
    prepare_emitters(prepared);
    const HiprtScene scene(device, std::move(prepared));
    auto failure = allocate_device<unsigned int>(1, "Allocating terminal-bounce MIS failures");
    check_hip(hipMemset(failure.get(), 0, sizeof(unsigned int)), "Clearing terminal-bounce MIS failures");
    hipLaunchKernelGGL(check_terminal_bounce_mis, dim3(1), dim3(256), 0, 0, scene.scene(), failure.get());
    check_hip(hipGetLastError(), "Launching terminal-bounce MIS checks");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost),
              "Reading terminal-bounce MIS checks");
    if (result)
        throw std::runtime_error("Terminal-bounce MIS checks failed (flags " + std::to_string(result) + ")");
}

__global__ void check_near_field_mis(TraceScene scene, NearFieldSample* results) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    const Ray ray{{0, 0.01f, 0}, {0, -1, 0}};
    const auto seed = index * 2891336453u + 741103597u;
    Random random{seed};
    results[index].mis = trace(ray, random, scene).x / 2;
    random = {seed};
    Hit floor{};
    if (!hit_scene(ray, floor, scene)) __builtin_trap();
    const auto light = sample_light(random, scene);
    const auto to_light = light.point - (floor.point + floor.geometric_normal * 0.002f);
    const float distance_squared = dot(to_light, to_light);
    const auto direction = to_light / sqrtf(distance_squared);
    const float cosine = fmaxf(0, dot(floor.normal, direction));
    const float light_cosine = fmaxf(0, dot(light.normal, -direction));
    results[index].nee = evaluate_scattering(ray, floor, direction, cosine).x *
        (light_cosine / (light.area_pdf * distance_squared));
}

inline void run_near_field_mis_tests(int device, SceneDescription description) {
    constexpr unsigned int count = 65536;
    auto results = allocate_device<NearFieldSample>(count, "Allocating near-field MIS comparisons");
    for (auto& texture : description.settings.room.textures) texture = ConstantColor{{0, 0, 0}};
    const auto white = ConstantColor{{1, 1, 1}};
    for (const auto& material : {Material{Diffuse{white}}, Material{Phong{white}}, Material{Metal{white, 0}}}) {
        description.objects = {{hip_demo::Rectangle{{0, 0, 0}, 40, 40}, material}};
        auto prepared = prepare_scene(description);
        append_triangle(prepared.vertices, {-1, 0.02f, -1}, {1, 0.02f, -1}, {1, 0.02f, 1});
        append_triangle(prepared.vertices, {-1, 0.02f, -1}, {1, 0.02f, 1}, {-1, 0.02f, 1});
        prepared.triangle_materials.resize(prepared.vertices.size() / 3,
                                          {prepare_material(Emissive{ConstantColor{{2, 3, 4}}}), 1});
        prepare_emitters(prepared);
        const HiprtScene scene(device, std::move(prepared));
        hipLaunchKernelGGL(check_near_field_mis, dim3(count / 256), dim3(256), 0, 0, scene.scene(), results.get());
        check_hip(hipGetLastError(), "Launching near-field MIS comparison");
        std::vector<NearFieldSample> samples(count);
        check_hip(hipMemcpy(samples.data(), results.get(), count * sizeof(NearFieldSample), hipMemcpyDeviceToHost),
                  "Reading near-field MIS comparison");
        double mean = 0, nee_mean = 0, square = 0, nee_square = 0;
        for (const auto sample : samples) {
            if (!std::isfinite(sample.mis) || !std::isfinite(sample.nee))
                throw std::runtime_error("Near-field sampling produced nonfinite radiance");
            mean += sample.mis; square += double(sample.mis) * sample.mis;
            nee_mean += sample.nee; nee_square += double(sample.nee) * sample.nee;
        }
        mean /= count; nee_mean /= count;
        const double variance = square / count - mean * mean;
        const double nee_variance = nee_square / count - nee_mean * nee_mean;
        const bool phong = std::holds_alternative<Phong>(material);
        const double expected = phong
            ? 0.5 + 0.5 * phong_specular_reflectance * (phong_exponent + 1) / (phong_exponent + 2) : 1;
        if (std::abs(mean - expected) > 0.02)
            throw std::runtime_error("Near-field MIS changed the expected reflected energy");
        if (std::holds_alternative<Metal>(material)) {
            if (std::abs(variance) > 1e-10)
                throw std::runtime_error("Delta-bounce emission was incorrectly MIS-weighted");
        } else {
            if (!(nee_variance > 20 * variance))
                throw std::runtime_error("MIS did not reduce near-field variance by at least 20x");
            std::cout << "Near-field " << (phong ? "Phong" : "diffuse") << ": mean " << mean
                      << ", MIS variance " << variance << ", NEE variance " << nee_variance
                      << " (" << nee_variance / variance << "x reduction).\n";
        }
    }
}

inline void run_emission_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    description.objects.clear();
    description.settings.room.size = {40, 40, 40};
    run_occlusion_tests(device, description);
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
    hipLaunchKernelGGL(check_mis_bsdf, dim3(1), dim3(1), 0, 0, failure.get());
    check_hip(hipGetLastError(), "Launching MIS BSDF checks");
    unsigned int bsdf_failure = 0;
    check_hip(hipMemcpy(&bsdf_failure, failure.get(), sizeof(bsdf_failure), hipMemcpyDeviceToHost),
              "Reading MIS BSDF checks");
    if (bsdf_failure)
        throw std::runtime_error("MIS BSDF checks failed (flags " + std::to_string(bsdf_failure) + ")");
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
    run_near_field_mis_tests(device, description);
    run_terminal_bounce_mis_test(device, description);
    std::cout << "Emissive objects, transformed mesh sampling, multiple lights and emission transport checks passed.\n";
}
}
