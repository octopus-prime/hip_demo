#include "../src2/renderer.h"
#include "../src2/mesh.h"
#include "../src2/dielectric.h"
#include "../src2/path_scattering.h"
#include "../src2/area_light.h"

#include <png.h>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void expect_error(Function operation)
{
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected operation to throw.");
}

__global__ void generate_camera_ray(GpuCamera camera, unsigned int x, unsigned int y,
                                    int width, int height, float2 offset, hiprtRay* result)
{
    *result = camera.generate_ray(x, y, width, height, offset);
}

hiprtRay camera_ray(const Camera& camera, unsigned int x, unsigned int y, int width, int height,
                   float2 offset = {0.5f, 0.5f})
{
    DeviceBuffer<hiprtRay> result(1);
    hipLaunchKernelGGL(generate_camera_ray, dim3(1), dim3(1), 0, 0,
                      camera.get(), x, y, width, height, offset, result.get());
    check_hip(hipGetLastError(), "Launching camera test");
    check_hip(hipDeviceSynchronize(), "Waiting for camera test");
    hiprtRay ray{};
    result.download(std::span<hiprtRay>(&ray, 1));
    return ray;
}

void require_vector(float3 actual, float3 expected, const char* message)
{
    require(std::abs(actual.x - expected.x) < 0.00001f &&
            std::abs(actual.y - expected.y) < 0.00001f &&
            std::abs(actual.z - expected.z) < 0.00001f, message);
}

std::vector<unsigned char> read_pixels(const std::filesystem::path& path);

__global__ void dielectric_kernel(float3 direction, float ior, DielectricSplit* result)
{
    *result = split_dielectric(direction, {0, 0, 1}, ior);
}

DielectricSplit dielectric_sample(float3 direction, float ior)
{
    DeviceBuffer<DielectricSplit> result(1);
    hipLaunchKernelGGL(dielectric_kernel, dim3(1), dim3(1), 0, 0, direction, ior, result.get());
    check_hip(hipGetLastError(), "Launching dielectric test");
    check_hip(hipDeviceSynchronize(), "Waiting for dielectric test");
    DielectricSplit sample;
    result.download(std::span<DielectricSplit>(&sample, 1));
    return sample;
}

void check_dielectric_optics()
{
    for (float z : {-1.0f, 1.0f}) {
        const auto normal = dielectric_sample({0, 0, z}, 1.5f);
        require(std::abs(normal.fresnel - 0.04f) < 0.00001f,
                "Air/glass normal-incidence Fresnel reflectance must be four percent on both sides.");
        require_vector(normal.transmitted, {0, 0, z}, "Normal incidence must not bend the ray.");
    }
    const auto entering = dielectric_sample({0.6f, 0, -0.8f}, 1.5f);
    require_vector(entering.transmitted, {0.4f, 0, -std::sqrt(0.84f)},
                   "Entering glass must obey Snell's law.");
    const auto leaving = dielectric_sample({0.4f, 0, std::sqrt(0.84f)}, 1.5f);
    require_vector(leaving.transmitted, {0.6f, 0, 0.8f}, "Leaving glass must undo the refraction.");
    require(std::abs(entering.fresnel - leaving.fresnel) < 0.00001f,
            "Fresnel reflectance must agree for reciprocal air/glass paths.");
    const auto tir = dielectric_sample({0.8f, 0, 0.6f}, 1.5f);
    require(tir.fresnel == 1, "Exiting above the critical angle must reflect all light.");
    const auto matched = dielectric_sample({0.6f, 0, -0.8f}, 1);
    require(matched.fresnel == 0, "Equal refractive indices must have no Fresnel reflection.");
    require_vector(matched.transmitted, {0.6f, 0, -0.8f}, "Equal refractive indices must not bend rays.");
}

__global__ void path_scatter_kernel(SurfaceMaterial material, float3 incoming, float3 normal,
                                    PathScatter* samples, unsigned int count)
{
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) {
        hip_demo::Random random{(i + 1u) * 747796405u};
        samples[i] = sample_path_scatter<true>(material, incoming, normal, random);
    }
}

std::vector<PathScatter> path_scatter_samples(SurfaceMaterial material, float3 incoming, float3 normal)
{
    constexpr unsigned int count = 16384;
    DeviceBuffer<PathScatter> device(count);
    hipLaunchKernelGGL(path_scatter_kernel, dim3(count / 256), dim3(256), 0, 0,
                      material, incoming, normal, device.get(), count);
    check_hip(hipGetLastError(), "Launching path-scattering test");
    check_hip(hipDeviceSynchronize(), "Waiting for path-scattering test");
    std::vector<PathScatter> samples(count);
    device.download(samples);
    return samples;
}

void check_path_scattering()
{
    const float3 albedo{0.2f, 0.4f, 0.6f};
    const auto normal = hiprt::normalize(float3{1, 2, 3});
    const auto diffuse = path_scatter_samples({.base_color = albedo}, -normal, normal);
    float mean_cosine = 0;
    for (const auto& scatter : diffuse) {
        const float cosine = hiprt::dot(scatter.direction, normal);
        require(std::isfinite(cosine) && cosine >= 0 &&
                std::abs(hiprt::dot(scatter.direction, scatter.direction) - 1) < 0.00001f,
                "Diffuse samples must be normalized and remain in the oriented hemisphere.");
        require_vector(scatter.weight, albedo, "Cosine-weighted Lambert scattering must carry the albedo.");
        require(std::abs(scatter.pdf - cosine / hip_demo::pi) < 0.00001f,
                "The diffuse continuation must report its solid-angle PDF.");
        mean_cosine += cosine / diffuse.size();
    }
    require(std::abs(mean_cosine - 2.0f / 3.0f) < 0.015f,
            "Diffuse scattering must have the cosine-weighted, not uniform, angular distribution.");

    const auto mixture = path_scatter_samples(
        {.base_color = albedo, .reflectivity = 0.25f, .transmission = 0.5f}, {0, 0, -1}, {0, 0, 1});
    int reflected = 0;
    int transmitted = 0;
    for (const auto& scatter : mixture) {
        if (scatter.weight.x == 1) {
            ++reflected;
            require_vector(scatter.direction, {0, 0, 1}, "The sampled reflection direction is wrong.");
            require_vector(scatter.weight, {1, 1, 1}, "Do not multiply Fresnel or mirror weights twice.");
            require(scatter.pdf == 0, "A delta reflection must not compete with area-light sampling.");
        } else if (std::abs(scatter.weight.x - 4.0f / 9.0f) < 0.00001f) {
            ++transmitted;
            require_vector(scatter.direction, {0, 0, -1}, "Normal-incidence transmission must remain straight.");
            require(scatter.pdf == 0, "Delta transmission must not compete with area-light sampling.");
        } else {
            require_vector(scatter.weight, albedo, "The remaining mixture samples must be diffuse.");
            require(std::abs(scatter.pdf - 0.375f * scatter.direction.z / hip_demo::pi) < 0.00001f,
                    "MIS must use the full mixture PDF, including diffuse selection probability.");
        }
    }
    require(std::abs(static_cast<float>(reflected) / mixture.size() - 0.265f) < 0.015f &&
            std::abs(static_cast<float>(transmitted) / mixture.size() - 0.36f) < 0.015f,
            "Mixed materials must sample mirror, Fresnel reflection, transmission and diffuse in proportion.");
    const auto tir = path_scatter_samples({.transmission = 1}, {0.8f, 0, 0.6f}, {0, 0, 1});
    for (const auto& scatter : tir) {
        require_vector(scatter.direction, {0.8f, 0, -0.6f}, "TIR must always sample the reflected direction.");
        require_vector(scatter.weight, {1, 1, 1}, "TIR must retain the full path throughput.");
    }
}

void check_path_transport(const Context& context, const Geometry& triangle, const std::filesystem::path& output)
{
    const std::size_t center = 3 * (9 * 17 + 8);
    const Camera camera({0, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    const std::array instances{
        Instance{.geometry = triangle, .material = {.base_color = {0.5f, 0.5f, 0.5f}}},
    };
    Scene scene(context, instances);
    scene.build();
    const PointLight dark_light({0, 2, 2}, {1, 1, 1}, 0);
    Renderer primary(17, 19, 0, 32, Integrator::path);
    primary.render(scene, camera, dark_light);
    primary.save(output);
    require(read_pixels(output)[center] == 0, "A depth-zero path cannot receive indirect background light.");
    Renderer bounced(17, 19, 1, 32, Integrator::path);
    bounced.render(scene, camera, dark_light);
    bounced.save(output);
    const auto environment = read_pixels(output);
    const std::array<int, 3> expected{6, 8, 12};
    for (int c = 0; c < 3; ++c) {
        require(std::abs(static_cast<int>(environment[center + c]) - expected[c]) <= 1,
                "One Lambert bounce must return albedo times background, without an extra cosine/PDF factor.");
    }
    bounced.render(scene, camera, dark_light);
    bounced.save(output);
    require(read_pixels(output) == environment, "Path samples must be reproducible across repeated renders.");

    Geometry plane(context, UnitPlane{});
    plane.build();
    const std::array room_instances{
        Instance{.geometry = plane, .scale = {4, 1, 4},
                 .material = {.base_color = {0.8f, 0.8f, 0.8f}}},
        Instance{.geometry = plane, .translation = {1, 2, 0}, .rotation_degrees = 90,
                 .scale = {2, 1, 4}, .material = {.base_color = {1, 0, 0}}},
    };
    Scene room(context, room_instances);
    room.build();
    const Camera floor_view({0, 1.5f, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    Renderer indirect(17, 19, 1, 4096, Integrator::path);
    indirect.render(room, floor_view, DirectionalLight({-1, 0, 0}));
    indirect.save(output);
    const auto bleeding = read_pixels(output);
    require(bleeding[center] > 15 && bleeding[center] > 1.3f * bleeding[center + 2],
            "A directly unlit white floor must receive red indirect light from the illuminated wall.");

    Geometry sphere(context, UnitSphere{});
    sphere.build();
    const std::array glass_instances{
        Instance{.geometry = sphere, .material = {.transmission = 1, .ior = 3}},
        Instance{.geometry = triangle, .translation = {0, 0, -2},
                 .material = {.base_color = {0.6f, 0.3f, 0.1f}}},
    };
    Scene glass(context, glass_instances);
    glass.build();
    const Camera glass_view({0, 0, 3}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    Renderer stochastic(17, 19, 2, 8192, Integrator::path);
    stochastic.render(glass, glass_view, DirectionalLight({1, 0, 1}));
    stochastic.save(output);
    const auto through_glass = read_pixels(output);
    const std::array<float, 3> color{0.6f, 0.3f, 0.1f};
    const std::array<int, 3> background{12, 16, 24};
    for (int c = 0; c < 3; ++c) {
        const int estimate = static_cast<int>(0.25f * background[c] +
            0.75f * 0.75f * 255 * color[c] / (hip_demo::pi * std::sqrt(2.0f)));
        require(std::abs(static_cast<int>(through_glass[center + c]) - estimate) <= 1,
                "Stochastic glass must converge to weighted reflection and two-interface transmission.");
    }
    const std::array inside_instances{
        Instance{.geometry = sphere, .material = {.transmission = 1}},
    };
    Scene inside(context, inside_instances);
    inside.build();
    const Camera inside_view({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 73.7397953f);
    Renderer exiting(17, 19, 1, 4096, Integrator::path);
    exiting.render(inside, inside_view);
    exiting.save(output);
    const auto escaped = read_pixels(output);
    for (int c = 0; c < 3; ++c) {
        require(std::abs(static_cast<int>(escaped[center + c]) -
                         static_cast<int>(0.96f * 2.25f * background[c])) <= 1,
                "Radiance transmission from inside glass must include eta squared and the correct exit bias.");
    }
}

struct RectangleCheck {
    WorldRectangle rectangle;
    double center_pdf;
    double two_light_pdf;
};

__global__ void rectangle_check_kernel(hiprtScene scene, RectangleCheck* result)
{
    const auto rectangle = world_rectangle(scene, 0);
    const auto from = rectangle.center + 2.0f * rectangle.normal;
    *result = {rectangle, rectangle_light_pdf(rectangle, 1, from, rectangle.center),
                         rectangle_light_pdf(rectangle, 2, from, rectangle.center)};
}

void check_area_lights(const Context& context, const std::filesystem::path& output)
{
    Geometry plane(context, UnitPlane{});
    plane.build();
    Geometry sphere(context, UnitSphere{});
    sphere.build();
    const std::array transformed{
        Instance{.geometry = plane, .translation = {1, 2, -3}, .rotation_degrees = 90,
                 .scale = {2, 3, 0.5f}, .emission = {1, 1, 1}},
    };
    Scene tilted(context, transformed);
    tilted.build();
    DeviceBuffer<RectangleCheck> device(1);
    hipLaunchKernelGGL(rectangle_check_kernel, dim3(1), dim3(1), 0, 0, tilted.get(), device.get());
    check_hip(hipGetLastError(), "Launching rectangle PDF test");
    check_hip(hipDeviceSynchronize(), "Waiting for rectangle PDF test");
    RectangleCheck geometry{};
    device.download(std::span<RectangleCheck>(&geometry, 1));
    require_vector(geometry.rectangle.center, {1, 2, -3}, "Rectangle translation was lost.");
    require_vector(geometry.rectangle.normal, {-1, 0, 0}, "Rectangle emission must rotate with its surface.");
    require(std::abs(geometry.rectangle.area - 4) < 0.00001f &&
            std::abs(geometry.center_pdf - 1) < 0.00001 &&
            std::abs(geometry.two_light_pdf - 0.5) < 0.00001,
            "Rectangle PDFs must include nonuniform world area and light-selection probability.");

    for (float value : {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity()}) {
        const std::array invalid{Instance{.geometry = plane, .emission = {value, 1, 1}}};
        expect_error<std::invalid_argument>([&] { Scene scene(context, invalid); });
    }
    const std::array unsupported{Instance{.geometry = sphere, .emission = {1, 1, 1}}};
    expect_error<std::invalid_argument>([&] { Scene scene(context, unsupported); });
    const std::array oversized{
        Instance{.geometry = plane, .scale = {1e30f, 1, 1e30f}, .emission = {1, 1, 1}}};
    expect_error<std::invalid_argument>([&] { Scene scene(context, oversized); });

    const auto render_center = [&](std::span<const Instance> instances, const Camera& camera,
                                   Integrator mode, int depth, int samples = 8192) {
        Scene scene(context, instances);
        scene.build();
        Renderer renderer(17, 19, depth, samples, mode);
        renderer.render(scene, camera, PointLight({0, 5, 0}, {1, 1, 1}, 0));
        renderer.save(output);
        const auto pixels = read_pixels(output);
        const std::size_t center = 3 * (9 * 17 + 8);
        return std::array<int, 3>{pixels[center], pixels[center + 1], pixels[center + 2]};
    };
    const Camera down({0, 0.25f, 0}, {0, 0, 0}, {0, 0, -1}, 1);
    const Camera below({0, -0.25f, 0}, {0, 0, 0}, {0, 0, 1}, 1);
    const std::array glowing{Instance{.geometry = plane, .emission = {0.25f, 0.5f, 0.75f}}};
    const std::array mirror{
        Instance{.geometry = plane, .material = {.reflectivity = 1}},
        Instance{.geometry = plane, .translation = {0, 1, 0}, .rotation_degrees = 180,
                 .emission = {0.25f, 0.5f, 0.75f}},
    };
    for (auto mode : {Integrator::ray, Integrator::path}) {
        const auto camera_hit = render_center(glowing, down, mode, 0, 1);
        const auto back_hit = render_center(glowing, below, mode, 0, 1);
        const auto mirror_hit = render_center(mirror, down, mode, 1, 1);
        const std::array<int, 3> emission{63, 127, 191};
        for (int c = 0; c < 3; ++c) {
            require(std::abs(camera_hit[c] - emission[c]) <= 1 && back_hit[c] == 0 &&
                    std::abs(mirror_hit[c] - emission[c]) <= 1,
                    "One-sided emission must be unweighted for camera and delta-reflected hits.");
        }
    }

    const std::array full{
        Instance{.geometry = plane, .scale = {4, 1, 4}, .material = {.base_color = {0.5f, 0, 0}}},
        Instance{.geometry = plane, .translation = {0, 1, 0}, .rotation_degrees = 180,
                 .emission = {0.75f, 0, 0}},
    };
    const float view_factor = 4.0f / (hip_demo::pi * std::sqrt(2.0f)) * std::atan(1.0f / std::sqrt(2.0f));
    const float direct = 255 * 0.5f * 0.75f * view_factor;
    const auto nee_only = render_center(full, down, Integrator::path, 0);
    const auto mis = render_center(full, down, Integrator::path, 1);
    const auto ray = render_center(full, down, Integrator::ray, 0);
    const float environment = 0.5f * 12 * (1 - view_factor);
    require(std::abs(nee_only[0] - direct) <= 2 && std::abs(ray[0] - direct) <= 2 &&
            std::abs(mis[0] - (direct + environment)) <= 2,
            "Area-light energy must match the analytic form factor with and without a competing BSDF path.");
    auto mixed = full;
    mixed[0].material.reflectivity = 0.5f;
    const auto mixed_mis = render_center(mixed, down, Integrator::path, 1);
    require(std::abs(mixed_mis[0] - (0.5f * (direct + environment) + 0.5f * 0.75f * 255)) <= 2,
            "MIS must use the full mixture PDF without attenuating delta emission or double-counting light.");

    const std::array halves{
        full[0],
        Instance{.geometry = plane, .translation = {-0.5f, 1, 0}, .rotation_degrees = 180,
                 .scale = {0.5f, 1, 1}, .emission = {0.75f, 0, 0}},
        Instance{.geometry = plane, .translation = {0.5f, 1, 0}, .rotation_degrees = 180,
                 .scale = {0.5f, 1, 1}, .emission = {0.75f, 0, 0}},
    };
    const auto split = render_center(halves, down, Integrator::path, 1);
    require(std::abs(split[0] - (direct + environment)) <= 2,
            "Splitting an emitter must preserve brightness, including selection PDFs on BSDF light hits.");
    const std::array partial{
        full[0], full[1],
        Instance{.geometry = plane, .translation = {0.5f, 0.5f, 0}, .scale = {0.5f, 1, 1}},
    };
    const auto penumbra = render_center(partial, down, Integrator::path, 0);
    require(std::abs(penumbra[0] - 0.5f * direct) <= 2,
            "A half-visible emitter must produce partial illumination, not a binary point-light shadow.");
    const std::array beyond{
        full[0], full[1],
        Instance{.geometry = plane, .translation = {0, 2, 0}, .scale = {4, 1, 4}},
    };
    const auto unblocked = render_center(beyond, down, Integrator::path, 0);
    require(std::abs(unblocked[0] - direct) <= 2, "Geometry beyond the sampled emitter must not shadow it.");

    const std::array glass{
        Instance{.geometry = sphere, .material = {.transmission = 1, .ior = 3}},
        Instance{.geometry = plane, .translation = {0, 0, -2}, .rotation_axis = {1, 0, 0},
                 .rotation_degrees = 90, .emission = {0.25f, 0.5f, 0.75f}},
    };
    const Camera front({0, 0, 3}, {0, 0, 0}, {0, 1, 0}, 1);
    for (auto mode : {Integrator::ray, Integrator::path}) {
        const auto transmitted = render_center(glass, front, mode, 2);
        const std::array<float, 3> emission{0.25f, 0.5f, 0.75f};
        const std::array<int, 3> background{12, 16, 24};
        for (int c = 0; c < 3; ++c) {
            const float expected = 0.25f * background[c] + 0.75f * 0.75f * 255 * emission[c];
            require(std::abs(transmitted[c] - expected) <= 2,
                    "Glass-emitter hits must keep full delta weight and retain deferred ray siblings.");
        }
    }
}

void check_glass(const Context& context, const Geometry& triangle, const std::filesystem::path& output)
{
    Geometry sphere(context, UnitSphere{});
    sphere.build();
    const std::size_t center = 3 * (9 * 17 + 8);
    const Camera camera({0, 0, 3}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    Renderer renderer(17, 19, 2);
    const std::array<float, 3> color{0.6f, 0.3f, 0.1f};
    const std::array<int, 3> background{12, 16, 24};
    for (float ior : {1.0f, 1.5f, 3.0f}) {
        const std::array instances{
            Instance{.geometry = sphere, .material = {.transmission = 1, .ior = ior}},
            Instance{.geometry = triangle, .translation = {0, 0, -2},
                     .material = {.base_color = {color[0], color[1], color[2]}}},
        };
        Scene scene(context, instances);
        scene.build();
        renderer.render(scene, camera, DirectionalLight({1, 0, 1}));
        renderer.save(output);
        const auto pixels = read_pixels(output);
        const float ratio = (1 - ior) / (1 + ior);
        const float f = ratio * ratio;
        for (int c = 0; c < 3; ++c) {
            const int expected = static_cast<int>(background[c] * f +
                255 * color[c] / std::sqrt(2.0f) * (1 - f) * (1 - f));
            require(std::abs(static_cast<int>(pixels[center + c]) - expected) <= 1,
                    "Glass must add reflected background and light transmitted through both interfaces.");
        }
    }

    const std::array inside_instances{
        Instance{.geometry = sphere, .material = {.transmission = 1}},
    };
    Scene inside(context, inside_instances);
    inside.build();
    const Camera inside_camera({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 73.7397953f);
    Renderer shallow(17, 19, 1);
    shallow.render(inside, inside_camera);
    shallow.save(output);
    const auto escaped = read_pixels(output);
    for (int c = 0; c < 3; ++c) {
        require(std::abs(static_cast<int>(escaped[center + c]) -
                         static_cast<int>(0.96f * background[c])) <= 1,
                "A camera inside glass must transmit out without immediately hitting the same surface.");
    }
    const std::array tir_instances{
        inside_instances[0],
        Instance{.geometry = sphere, .translation = {0.768f, 0, 0.224f}, .scale = {0.08f, 0.08f, 0.08f},
                 .material = {.base_color = {0.4f, 0.2f, 0.1f}}},
    };
    Scene tir(context, tir_instances);
    tir.build();
    const Camera tir_camera({0, 0, 0.8f}, {1, 0, 0.8f}, {0, 1, 0}, 73.7397953f);
    shallow.render(tir, tir_camera, PointLight({0.55f, 0, 0.7f}));
    shallow.save(output);
    require(read_pixels(output)[center] > 100,
            "Total internal reflection must find the off-axis interior object without self-intersection.");

    const std::array layers{
        Instance{.geometry = sphere, .translation = {0, 0, 1.5f}, .scale = {0.35f, 0.35f, 0.35f},
                 .material = {.transmission = 1}},
        Instance{.geometry = sphere, .translation = {0, 0, 0.5f}, .scale = {0.35f, 0.35f, 0.35f},
                 .material = {.transmission = 1}},
        Instance{.geometry = sphere, .translation = {0, 0, -0.5f}, .scale = {0.35f, 0.35f, 0.35f},
                 .material = {.transmission = 1}},
        Instance{.geometry = sphere, .translation = {0, 0, -1.5f}, .scale = {0.35f, 0.35f, 0.35f},
                 .material = {.transmission = 1}},
    };
    Scene layered(context, layers);
    layered.build();
    Renderer deepest(17, 19, maximum_ray_depth);
    deepest.render(layered, camera);
    deepest.save(output);
    const auto pixels = read_pixels(output);
    const auto reference = [&](auto&& self, int boundary, int direction, int depth) -> float {
        if (boundary < 0 || boundary >= 8) {
            return 1;
        }
        if (depth == maximum_ray_depth) {
            return 0;
        }
        return 0.04f * self(self, boundary - direction, -direction, depth + 1) +
               0.96f * self(self, boundary + direction, direction, depth + 1);
    };
    const float energy = reference(reference, 0, 1, 0);
    for (int c = 0; c < 3; ++c) {
        require(std::abs(static_cast<int>(pixels[center + c]) -
                         static_cast<int>(background[c] * energy)) <= 1,
                "The full-depth deferred-ray stack must preserve every Fresnel branch and terminate.");
    }
}

void check_antialiasing(const Context& context, const Geometry& triangle, const std::filesystem::path& output)
{
    const std::array instances{
        Instance{.geometry = triangle, .material = {.base_color = {0.8f, 0.4f, 0.2f}}},
    };
    Scene scene(context, instances);
    scene.build();
    const Camera camera({0, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    const std::array<int, 3> surface{204, 102, 51};
    const std::array<int, 3> background{12, 16, 24};
    std::vector<unsigned char> previous;
    for (int samples : {1, 4, 16}) {
        Renderer renderer(17, 19, 3, samples);
        renderer.render(scene, camera);
        renderer.save(output);
        const auto pixels = read_pixels(output);
        bool blended = false;
        bool odd_coverage = false;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 3) {
            bool matched = false;
            for (int hits = 0; hits <= samples; ++hits) {
                bool consistent = true;
                for (int c = 0; c < 3; ++c) {
                    const int expected = (surface[c] * hits + background[c] * (samples - hits)) / samples;
                    consistent = consistent && std::abs(static_cast<int>(pixels[pixel + c]) - expected) <= 1;
                }
                if (consistent) {
                    matched = true;
                    blended = blended || (hits > 0 && hits < samples);
                    odd_coverage = odd_coverage || (hits > 0 && hits < samples && hits % 2 == 1);
                }
            }
            require(matched, "AA colors must average exactly the requested number of surface/background samples.");
        }
        if (samples > 1) {
            require(blended && odd_coverage, "AA must produce fractional edge coverage at the requested sample count.");
            require(pixels != previous, "Increasing the sample count must change the edge samples.");
        }
        renderer.render(scene, camera);
        renderer.save(output);
        require(read_pixels(output) == pixels, "Repeated renders must use reproducible per-pixel sample positions.");
        previous = pixels;
    }

    Geometry plane(context, UnitPlane{});
    plane.build();
    const std::array bright_half_plane{
        Instance{.geometry = plane, .translation = {1, 0, 0}, .rotation_axis = {1, 0, 0},
                 .rotation_degrees = 90, .scale = {1, 1, 10},
                 .material = {.base_color = {1, 0, 0}, .specular_strength = 1, .shininess = 1}},
    };
    Scene bright(context, bright_half_plane);
    bright.build();
    Renderer renderer(17, 19, 3, 1024);
    renderer.render(bright, camera);
    renderer.save(output);
    const auto pixels = read_pixels(output);
    const std::size_t center = 3 * (9 * 17 + 8);
    require(pixels[center] > 220 && pixels[center + 1] > 80 && pixels[center + 1] < 180,
            "AA must average unclamped floating-point radiance before converting to RGB bytes.");
}

void check_unit_plane(const Context& context)
{
    Geometry plane(context, UnitPlane{});
    plane.build();
    for (float x : {-0.5f, 0.5f}) {
        for (float y : {-2.0f, 2.0f}) {
            hiprtRay ray{};
            ray.origin = {x, y, x * 0.5f};
            ray.direction = {0, y > 0 ? -1.0f : 1.0f, 0};
            const auto hit = plane.trace(ray);
            require(hit && std::abs(hit->t - 2) < 0.00001f,
                    "Both unit-plane triangles must intersect from either side.");
            require_vector(hit->normal, {0, 4, 0}, "Unit-plane winding must produce the +Y normal.");
        }
    }
    for (const auto origin : {float3{1.01f, 2, 0}, float3{0, 2, -1.01f}}) {
        hiprtRay ray{};
        ray.origin = origin;
        ray.direction = {0, -1, 0};
        require(!plane.trace(ray), "UnitPlane must be finite with local X/Z bounds [-1, 1].");
    }
    const std::array instances{
        Instance{.geometry = plane, .translation = {-3, 0, 0}, .scale = {2, 1, 0.5f}},
        Instance{.geometry = plane, .translation = {3, 0, 0}, .rotation_axis = {1, 0, 0},
                 .rotation_degrees = 90, .scale = {1, 1, 2}},
    };
    Scene scene(context, instances);
    scene.build();
    hiprtRay ray{};
    ray.origin = {-1.5f, 2, 0.25f};
    ray.direction = {0, -1, 0};
    auto hit = scene.trace(ray);
    require(hit && hit->instanceID == 0 && std::abs(hit->t - 2) < 0.00001f,
            "Unit-plane instance scaling or translation is incorrect.");
    ray.origin = {3, 0.5f, 3};
    ray.direction = {0, 0, -1};
    hit = scene.trace(ray);
    require(hit && hit->instanceID == 1 && std::abs(hit->t - 3) < 0.00001f,
            "A shared unit plane must also support a rotated wall instance.");
}

void check_reflections(const Context& context, const Geometry& triangle, const std::filesystem::path& output)
{
    const std::array<float3, 3> sloped{{{-1, -1, 1}, {1, -1, -1}, {0, 1, 0}}};
    Geometry mirror(context, sloped);
    mirror.build();
    const Camera camera({0, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    const std::size_t center = 3 * (9 * 17 + 8);
    const auto render = [&](float reflectivity, int limit, float target_x,
                            SurfaceMaterial target = {.base_color = {0.6f, 0.3f, 0.1f}}) {
        std::vector<Instance> instances{
            Instance{.geometry = mirror, .scale = {2, 1, 1}, .material = {.reflectivity = reflectivity}},
        };
        if (target_x != 0) {
            instances.push_back(Instance{
                .geometry = triangle, .translation = {target_x, 0, target_x * 0.75f},
                .rotation_axis = {0, 1, 0}, .rotation_degrees = target_x > 0 ? -90.0f : 90.0f,
                .material = target});
        }
        Scene scene(context, instances);
        scene.build();
        Renderer renderer(17, 19, limit);
        renderer.render(scene, target_x < 0 ? Camera({0, 0, -2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f) : camera,
                        DirectionalLight({target_x < 0 ? 1.0f : -1.0f, 0, 0}));
        renderer.save(output);
        const auto pixels = read_pixels(output);
        return std::array<int, 3>{pixels[center], pixels[center + 1], pixels[center + 2]};
    };
    const auto require_color = [](const std::array<int, 3>& actual, const std::array<int, 3>& expected,
                                   const char* message) {
        for (int c = 0; c < 3; ++c) {
            require(std::abs(actual[c] - expected[c]) <= 1, message);
        }
    };
    require_color(render(1, 1, 2), {153, 76, 25},
                  "A nonuniformly scaled mirror must reflect off-camera geometry with its material.");
    require_color(render(1, 1, -2), {153, 76, 25},
                  "Back-face reflections must offset to the outgoing side to avoid self-intersection.");
    require_color(render(0.5f, 1, 2), {76, 38, 12}, "Reflection throughput must respect reflectivity.");
    require_color(render(1, 0, 2), {0, 0, 0}, "The zero-reflection limit must not trace a secondary ray.");
    require_color(render(1, 1, 0), {12, 16, 24}, "An escaping reflected ray must return the background.");
    require_color(render(1, 1, 160), {153, 76, 25},
                  "Reflected rays must not inherit the camera's 100-unit distance limit.");
    require_color(render(0.5f, 1, 2, {.base_color = {1, 1, 1}, .specular_strength = 1, .shininess = 1}),
                  {229, 229, 229}, "Reflected radiance must be weighted before clamping to RGB bytes.");

    const std::array two_mirrors{
        Instance{.geometry = mirror, .scale = {2, 1, 1}, .material = {.reflectivity = 1}},
        Instance{.geometry = triangle, .translation = {2, 0, 1.5f},
                 .rotation_axis = {0, 1, 0}, .rotation_degrees = -90, .material = {.reflectivity = 1}},
        Instance{.geometry = triangle, .translation = {0, 0, 3},
                 .rotation_axis = {0, 1, 0}, .rotation_degrees = 180,
                 .material = {.base_color = {0.6f, 0.3f, 0.1f}}},
    };
    Scene scene(context, two_mirrors);
    scene.build();
    for (int limit : {1, 2}) {
        Renderer renderer(17, 19, limit);
        renderer.render(scene, camera, DirectionalLight({-1, 0, -1}));
        renderer.save(output);
        const auto pixels = read_pixels(output);
        require_color({pixels[center], pixels[center + 1], pixels[center + 2]},
                      limit == 1 ? std::array<int, 3>{0, 0, 0} : std::array<int, 3>{108, 54, 18},
                      "Two successive reflections must obey the depth limit and shade the final surface.");
    }

    const std::array facing_mirrors{
        Instance{.geometry = triangle, .rotation_axis = {0, 1, 0}, .rotation_degrees = 90,
                 .material = {.reflectivity = 1}},
        Instance{.geometry = triangle, .translation = {2, 0, 0},
                 .rotation_axis = {0, 1, 0}, .rotation_degrees = -90, .material = {.reflectivity = 1}},
    };
    Scene enclosed(context, facing_mirrors);
    enclosed.build();
    Renderer renderer(17, 19);
    renderer.render(enclosed, Camera({1, 0, 0}, {0, 0, 0}, {0, 1, 0}, 73.7397953f));
    renderer.save(output);
    const auto pixels = read_pixels(output);
    require_color({pixels[center], pixels[center + 1], pixels[center + 2]}, {0, 0, 0},
                  "Facing mirrors must terminate without artificial light or self-intersection.");
}

void check_ply_import(const Context& context, const std::filesystem::path& fixtures)
{
    for (const char* file : {"triangle_ascii.ply", "triangle_little.ply", "triangle_big.ply"}) {
        const auto vertices = load_ply_vertices((fixtures / file).string(), MeshUp::y);
        require(vertices.size() == 3, "PLY indices were not expanded into triangle vertices.");
        require_vector(vertices[0], {-1, -1, 0}, "PLY fit must preserve aspect ratio and center the mesh.");
        require_vector(vertices[1], {1, -1, 0}, "PLY vertex order changed.");
        require_vector(vertices[2], {0, 1, 0}, "PLY fit must center vertically.");
        Geometry geometry(context, vertices);
        geometry.build();
        const std::array instances{
            Instance{.geometry = geometry, .translation = {-2, 0, 0}},
            Instance{.geometry = geometry, .translation = {2, 0, 0}},
        };
        Scene scene(context, instances);
        scene.build();
        for (unsigned int i = 0; i < 2; ++i) {
            hiprtRay ray{};
            ray.origin = {i == 0 ? -2.0f : 2.0f, 0, 2};
            ray.direction = {0, 0, -1};
            const auto hit = scene.trace(ray);
            require(hit && hit->instanceID == i && std::abs(hit->t - 2) < 0.00001f,
                    "Shared PLY geometry must intersect through both scene instances.");
            require_vector(hit->normal, {0, 0, 4}, "PLY import must preserve outward triangle winding.");
        }
    }
    const auto oriented = load_ply_vertices((fixtures / "orientation_z_up.ply").string());
    require_vector(oriented[0], {-1, -1, 1}, "PLY Z-up conversion is incorrect.");
    require_vector(oriented[1], {1, -1, 1}, "PLY Z-up conversion must preserve winding.");
    require_vector(oriented[2], {0, 1, -1}, "PLY Z-up conversion or centering is incorrect.");
    expect_error<std::runtime_error>([&] { load_ply_vertices((fixtures / "does_not_exist.ply").string()); });
}

void check_box_and_torus(const Context& context, const Geometry& triangle, Renderer& renderer,
                         const std::filesystem::path& output)
{
    for (const auto radii : {Torus{0, 0.25f}, Torus{1, 0}, Torus{1, -0.25f}, Torus{1, 1},
                             Torus{1, 2}, Torus{std::numeric_limits<float>::infinity(), 0.25f},
                             Torus{1, std::numeric_limits<float>::quiet_NaN()},
                             Torus{1, std::numeric_limits<float>::denorm_min()},
                             Torus{1e20f, 1e19f}, Torus{1e-30f, 1e-31f}}) {
        expect_error<std::invalid_argument>([&] { Geometry invalid(context, radii); });
    }
    {
        Context other_context;
        Geometry foreign(other_context, UnitSphere{});
        foreign.build();
        const std::array foreign_instances{Instance{.geometry = foreign}};
        expect_error<std::invalid_argument>([&] { Scene invalid(context, foreign_instances); });
    }
    Geometry box(context, UnitBox{});
    box.build();
    for (const auto normal : std::array<float3, 6>{{{1,0,0}, {-1,0,0}, {0,1,0},
                                                  {0,-1,0}, {0,0,1}, {0,0,-1}}}) {
        hiprtRay ray{};
        ray.origin = 3.0f * normal;
        ray.direction = -normal;
        const auto hit = box.trace(ray);
        require(hit && std::abs(hit->t - 2) < 0.00001f, "Box mesh face was missed.");
        require_vector(hit->normal / 4.0f, normal, "Box triangle winding must give outward normals.");
    }
    Geometry torus(context, Torus{1, 0.25f});
    Geometry wide(context, Torus{2, 0.5f});
    torus.build();
    wide.build();
    hiprtRay ray{};
    ray.origin = {3, 0, 0};
    ray.direction = {-1, 0, 0};
    for (float distance : {1.75f, 2.25f, 3.75f, 4.25f}) {
        const auto hit = torus.trace(ray);
        require(hit && std::abs(hit->t - distance) < 0.0001f,
                "Torus quartic must preserve all four ordered intersections.");
        ray.minT = distance + 0.01f;
    }
    require(!torus.trace(ray), "Torus accepted a root before minT.");
    ray.minT = 0;
    ray.maxT = 1.7f;
    require(!torus.trace(ray), "Torus accepted a root beyond maxT.");
    ray.maxT = 100;
    ray.direction = {-2, 0, 0};
    auto hit = torus.trace(ray);
    require(hit && std::abs(hit->t - 0.875f) < 0.0001f, "Torus assumed a normalized ray direction.");
    require_vector(hit->normal, {1, 0, 0}, "Torus outer normal is incorrect.");
    ray.origin = {0, 3, 0};
    ray.direction = {0, -1, 0};
    require(!torus.trace(ray), "The torus hole was incorrectly filled by its AABB.");
    ray.origin = {1, 0, 0};
    ray.direction = {0, 1, 0};
    hit = torus.trace(ray);
    require(hit && std::abs(hit->t - 0.25f) < 0.0001f, "Ray inside the torus tube lost its exit.");
    require_vector(hit->normal, {0, 1, 0}, "Torus tube normal is incorrect.");
    ray.origin = {3, 0.25f, 0};
    ray.direction = {-1, 0, 0};
    hit = torus.trace(ray);
    require(hit && std::abs(hit->t - 2) < 0.0002f, "Torus tangent was missed.");

    const std::array instances{
        Instance{.geometry = torus, .translation = {-4, 0, 0}},
        Instance{.geometry = wide, .translation = {4, 0, 0}, .rotation_axis = {0, 0, 1},
                 .rotation_degrees = 90, .scale = {0.5f, 2, 0.75f},
                 .material = {.base_color = {0.2f, 0.6f, 0.9f}}},
        Instance{.geometry = box, .translation = {0, -4, 0}},
    };
    Scene scene(context, instances);
    scene.build();
    Geometry later(context, Torus{1.5f, 0.4f});
    later.build();
    ray.origin = {-2, 1, 0};
    ray.direction = {1, 0, 0};
    hit = scene.trace(ray);
    require(hit && hit->instanceID == 1 && std::abs(hit->t - 5) < 0.0002f,
            "Torus radii/function data were mixed between geometries or transformed incorrectly.");
    const Camera camera({-2, 1, 0}, {4, 1, 0}, {0, 0, 1}, 73.7397953f);
    renderer.render(scene, camera, DirectionalLight({-1, 0, 0}));
    renderer.save(output);
    const auto pixels = read_pixels(output);
    const std::size_t center = 3 * (9 * 17 + 8);
    const std::array<int, 3> expected{51, 153, 229};
    for (int c = 0; c < 3; ++c) {
        require(std::abs(static_cast<int>(pixels[center + c]) - expected[c]) <= 1,
                "Transformed torus material or inverse-transpose normal is incorrect.");
    }
    Geometry ring(context, Torus{0.4f, 0.1f});
    ring.build();
    const Camera front({0, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
    for (const bool blocked : {false, true}) {
        const std::array shadow_instances{
            Instance{.geometry = triangle},
            Instance{.geometry = ring, .translation = {blocked ? 0.6f : 1.0f, 0, 1},
                     .rotation_axis = {1, 0, 0}, .rotation_degrees = 90},
        };
        Scene shadow(context, shadow_instances);
        shadow.build();
        renderer.render(shadow, front, PointLight({2, 0, 2}, {1, 1, 1}, 8));
        renderer.save(output);
        const auto brightness = read_pixels(output)[center];
        require(blocked ? brightness == 0 : brightness > 0,
                "Torus any-hit query failed to distinguish the tube from the hole.");
    }
}

void check_shared_materials(const Context& context, const Geometry& triangle, Renderer& renderer,
                           const std::filesystem::path& output)
{
    const SurfaceMaterial base{.base_color = {0.4f, 0.2f, 0.1f}};
    auto wide = base;
    wide.shininess = 8;
    auto shiny = base;
    shiny.specular_strength = 0.2f;
    auto signed_zero = base;
    signed_zero.specular_strength = -0.0f;
    signed_zero.reflectivity = -0.0f;
    auto reflective = base;
    reflective.reflectivity = 0.5f;
    const SurfaceMaterial other{.base_color = {0.1f, 0.6f, 0.3f}};
    const std::array glass_instances{
        Instance{.geometry = triangle, .material = {.transmission = 1}},
        Instance{.geometry = triangle, .material = {.transmission = 1, .ior = 1.33f}},
        Instance{.geometry = triangle},
    };
    Scene glass_palette(context, glass_instances);
    require(glass_palette.material_count() == 3, "Transmission and IOR must distinguish shared materials.");
    const std::array materials{base, wide, base, shiny, signed_zero, other, reflective};
    std::vector<Instance> instances;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        instances.push_back(Instance{.geometry = triangle,
            .translation = {3.0f * static_cast<float>(i), 0, 0}, .material = materials[i]});
    }
    Scene scene(context, instances);
    require(scene.material_count() == 5, "Repeated materials were not shared, or distinct fields were merged.");
    std::array<std::uint32_t, 7> ids{};
    check_hip(hipMemcpy(ids.data(), scene.material_ids(), sizeof(ids), hipMemcpyDeviceToHost),
              "Reading shared material IDs");
    require(ids == std::array<std::uint32_t, 7>{0, 1, 0, 2, 0, 3, 4},
            "Material IDs must reuse equal values, including signed zero, across nonadjacent instances.");
    scene.build();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const float x = instances[i].translation.x;
        const Camera camera({x, 0, 2}, {x, 0, 0}, {0, 1, 0}, 73.7397953f);
        renderer.render(scene, camera);
        renderer.save(output);
        const auto pixels = read_pixels(output);
        const auto& material = materials[i];
        const std::array<float, 3> color{material.base_color.x, material.base_color.y, material.base_color.z};
        const std::array<int, 3> background{12, 16, 24};
        for (int c = 0; c < 3; ++c) {
            const int expected = static_cast<int>(
                255 * (color[c] + material.specular_strength) * (1 - material.reflectivity) +
                background[c] * material.reflectivity);
            require(std::abs(static_cast<int>(pixels[3 * (9 * 17 + 8) + c]) - expected) <= 1,
                    "GPU shading selected the wrong shared material for an instance.");
        }
    }
}

void check_shared_mesh_and_spheres(const Context& context)
{
    const std::array<float3, 6> vertices{{
        {-1, -1, 0}, {1, -1, 0}, {-1, 1, 0},
        {1, -1, 0}, {1, 1, 0}, {-1, 1, 0},
    }};
    expect_error<std::invalid_argument>([&] { Geometry invalid(context, std::span<const float3>{}); });
    expect_error<std::invalid_argument>([&] { Geometry invalid(context, std::span(vertices).first(4)); });
    auto invalid_vertices = vertices;
    invalid_vertices[4].x = std::numeric_limits<float>::quiet_NaN();
    expect_error<std::invalid_argument>([&] { Geometry invalid(context, invalid_vertices); });
    Geometry mesh(context, vertices);
    Geometry sphere(context, UnitSphere{});
    mesh.build();
    sphere.build();
    const std::array instances{
        Instance{.geometry = sphere, .translation = {-2, -3, 0}},
        Instance{.geometry = sphere, .translation = {-2, 0, 0}},
        Instance{.geometry = sphere, .translation = {-2, 3, 0}},
        Instance{.geometry = mesh, .translation = {2, -3, 0}},
        Instance{.geometry = mesh, .translation = {2, 0, 0}},
        Instance{.geometry = mesh, .translation = {2, 3, 0}},
    };
    Scene scene(context, instances);
    scene.build();
    for (unsigned int i = 0; i < instances.size(); ++i) {
        hiprtRay ray{};
        ray.origin = instances[i].translation + float3{0.25f, 0.25f, 4};
        ray.direction = {0, 0, -1};
        const auto hit = scene.trace(ray);
        const float distance = i < 3 ? 4.0f - std::sqrt(0.875f) : 4.0f;
        require(hit && hit->instanceID == i && hit->primID == (i < 3 ? 0u : 1u) &&
                std::abs(hit->t - distance) < 0.00001f,
                "Six-instance scene failed to reuse both geometries or select the second mesh triangle.");
    }
    hiprtRay ray{};
    ray.origin = {-0.25f, -0.25f, 2};
    ray.direction = {0, 0, -1};
    const auto hit = mesh.trace(ray);
    require(hit && hit->primID == 0 && std::abs(hit->t - 2) < 0.00001f,
            "Multi-triangle geometry lost its first triangle.");
}

void check_sphere(const Context& context, const Geometry& triangle, const Camera& camera,
                  Renderer& renderer, const std::filesystem::path& output)
{
    Geometry sphere(context, UnitSphere{});
    expect_error<std::logic_error>([&] { sphere.get(); });
    sphere.build();
    hiprtRay ray{};
    ray.origin = {0, 0, 3};
    ray.direction = {0, 0, -1};
    auto hit = sphere.trace(ray);
    require(hit && hit->primID == 0 && std::abs(hit->t - 2) < 0.00001f,
            "Analytic sphere front hit is incorrect.");
    require_vector(hit->normal, {0, 0, 1}, "Sphere normal must point outward.");
    ray.minT = 2.5f;
    hit = sphere.trace(ray);
    require(hit && std::abs(hit->t - 4) < 0.00001f, "Clipping entry must preserve the sphere exit.");
    ray.maxT = 3.5f;
    require(!sphere.trace(ray), "Sphere ignored the ray interval.");
    ray.minT = 0;
    ray.maxT = 1.5f;
    require(!sphere.trace(ray), "Sphere ignored a maximum before its first root.");
    ray.maxT = 100;
    ray.origin = {0, 0, 0};
    ray.direction = {0, 0, 2};
    hit = sphere.trace(ray);
    require(hit && std::abs(hit->t - 0.5f) < 0.00001f,
            "Sphere must support inside origins and nonunit directions.");
    ray.origin = {0.6f, 0.8f, 3};
    ray.direction = {0, 0, -1};
    hit = sphere.trace(ray);
    require(hit && std::abs(hit->t - 3) < 0.0001f, "Sphere tangent was missed.");
    ray.origin = {0.9f, 0.9f, 3};
    require(!sphere.trace(ray), "AABB candidate was incorrectly accepted as a sphere hit.");
    ray.origin = {0, 0, 3};
    ray.direction = {0, 0, 1};
    require(!sphere.trace(ray), "Sphere accepted roots behind the ray.");

    const std::array instances{
        Instance{.geometry = triangle, .translation = {-3, 0, 0}},
        Instance{.geometry = sphere, .translation = {1, 0, 0}, .scale = {2, 1, 0.5f}},
    };
    Scene scene(context, instances);
    scene.build();
    ray.origin = {2, 0, 3};
    ray.direction = {0, 0, -1};
    hit = scene.trace(ray);
    const float z = std::sqrt(0.75f);
    require(hit && hit->instanceID == 1 && hit->primID == 0 &&
            std::abs(hit->t - (3 - 0.5f * z)) < 0.00001f,
            "Mixed scene lost the custom primitive or its nonuniform transform.");
    require_vector(hit->normal, {0.5f, 0, z}, "Transformed sphere must return a local normal.");

    const std::array visible{Instance{.geometry = sphere, .scale = {1, 1, 0.5f},
                                     .material = {.base_color = {0.8f, 0.4f, 0.2f}}}};
    Scene visible_scene(context, visible);
    visible_scene.build();
    renderer.render(visible_scene, camera);
    renderer.save(output);
    const auto pixels = read_pixels(output);
    const std::size_t center = 3 * (9 * 17 + 8);
    require(std::abs(static_cast<int>(pixels[center]) - 204) <= 1,
            "Custom sphere did not render or self-shadowed.");

    const std::array blocked{
        Instance{.geometry = triangle},
        Instance{.geometry = sphere, .translation = {1, 0, 1}, .scale = {0.2f, 0.2f, 0.2f}},
    };
    Scene shadow_scene(context, blocked);
    shadow_scene.build();
    renderer.render(shadow_scene, camera, PointLight({2, 0, 2}));
    renderer.save(output);
    require(read_pixels(output)[center] == 0, "Custom sphere failed the any-hit shadow query.");
    renderer.render(shadow_scene, camera, PointLight({0.5f, 0, 0.5f}));
    renderer.save(output);
    require(read_pixels(output)[center] > 0, "Sphere beyond the point light cast a shadow.");
}

void check_camera()
{
    const float3 position{0.0f, 0.0f, 2.0f};
    const float3 target{0.0f, 0.0f, 0.0f};
    const float3 up{0.0f, 1.0f, 0.0f};
    for (float fov : {0.0f, 180.0f, std::numeric_limits<float>::quiet_NaN()}) {
        expect_error<std::invalid_argument>([&] { Camera camera(position, target, up, fov); });
    }
    expect_error<std::invalid_argument>([&] { Camera camera(position, position, up, 90.0f); });
    expect_error<std::invalid_argument>([&] { Camera camera(position, target, {0, 0, 0}, 90.0f); });
    expect_error<std::invalid_argument>([&] { Camera camera(position, target, {0, 0, 1}, 90.0f); });
    expect_error<std::invalid_argument>([&] {
        Camera camera(position, {std::numeric_limits<float>::infinity(), 0, 0}, up, 90.0f);
    });
    expect_error<std::invalid_argument>([&] {
        Camera camera({-std::numeric_limits<float>::max(), 0, 0},
                      {std::numeric_limits<float>::max(), 0, 0}, up, 90.0f);
    });
    expect_error<std::invalid_argument>([&] {
        Camera camera(position, target, {0, std::numeric_limits<float>::max(), 0}, 90.0f);
    });
    expect_error<std::invalid_argument>([&] {
        Camera camera(position, target, {0, 1.0e-30f, 0}, 90.0f);
    });

    const Camera camera(position, target, up, 90.0f);
    const auto center = camera_ray(camera, 1, 1, 3, 3);
    require_vector(center.origin, position, "Incorrect ray origin.");
    require_vector(center.direction, {0, 0, -1}, "Center ray does not point at the target.");
    require_vector(camera_ray(camera, 0, 0, 1, 1, {0.25f, 0.75f}).direction,
                   {-1 / std::sqrt(6.0f), -1 / std::sqrt(6.0f), -2 / std::sqrt(6.0f)},
                   "Subpixel camera offsets must move rays within their pixel.");
    require_vector(camera_ray(camera, 4, 1, 5, 3).direction, {0.8f, 0, -0.6f},
                   "Incorrect aspect ratio or ray normalization.");

    const Camera turned({3, 0, 0}, target, up, 90.0f);
    require_vector(camera_ray(turned, 1, 1, 3, 3).direction, {-1, 0, 0},
                   "Camera target does not rotate the view.");
    const Camera rolled(position, target, {1, 0, 0}, 90.0f);
    require_vector(camera_ray(rolled, 1, 0, 3, 3).direction,
                   {2 / std::sqrt(13.0f), 0, -3 / std::sqrt(13.0f)},
                   "Camera up does not rotate the image axes.");
    const Camera narrow(position, target, up, 45.0f);
    const auto wide_ray = camera_ray(camera, 2, 1, 3, 3);
    const auto narrow_ray = camera_ray(narrow, 2, 1, 3, 3);
    const float slope = (2.0f / 3.0f) * (std::sqrt(2.0f) - 1.0f);
    const float length = std::sqrt(slope * slope + 1);
    require_vector(narrow_ray.direction, {slope / length, 0, -1 / length}, "Incorrect field of view.");
    require(narrow_ray.direction.x < wide_ray.direction.x, "Narrower FOV did not narrow the ray cone.");
}

void check_scene(const Context& context, const Geometry& triangle)
{
    expect_error<std::invalid_argument>([&] { Scene empty(context, {}); });
    const std::array invalid_instances{
        Instance{triangle, {std::numeric_limits<float>::infinity(), 0, 0}},
    };
    expect_error<std::invalid_argument>([&] { Scene invalid(context, invalid_instances); });

    const std::array instances{
        Instance{triangle, {-1.15f, 0, 0}},
        Instance{triangle, { 1.15f, 0, 0}},
    };
    Scene scene(context, instances);
    expect_error<std::logic_error>([&] { scene.get(); });
    scene.build();

    hiprtRay ray{};
    ray.direction = {0, 0, -1};
    ray.minT = 0.001f;
    ray.maxT = 100.0f;
    for (unsigned int i = 0; i < instances.size(); ++i) {
        ray.origin = {instances[i].translation.x, 0, 2};
        const auto hit = scene.trace(ray);
        require(hit && hit->instanceID == i && hit->primID == 0 &&
                std::abs(hit->t - 2.0f) < 0.00001f &&
                std::abs(hit->uv.x - 0.25f) < 0.00001f &&
                std::abs(hit->uv.y - 0.5f) < 0.00001f,
                "Translated instances did not retain their IDs and shared primitive.");
        ray.maxT = 1.0f;
        require(!scene.trace(ray), "Scene traversal ignored the ray distance limit.");
        ray.maxT = 100.0f;
    }
    ray.origin = {0, 0, 2};
    require(!scene.trace(ray), "Expected a miss between the instances.");
    ray.origin = {-0.25f, 0.9f, 2};
    require(!scene.trace(ray), "A bounding-box hit was mistaken for a triangle hit.");

    const std::array overlapping{
        Instance{triangle, {0, 0, 0}},
        Instance{triangle, {0, 0, 0.5f}},
    };
    Scene layered(context, overlapping);
    layered.build();
    ray.origin = {0, 0, 2};
    const auto closest = layered.trace(ray);
    require(closest && closest->instanceID == 1 && closest->primID == 0 &&
            std::abs(closest->t - 1.5f) < 0.00001f,
            "Scene traversal did not return the nearest instance.");
}

void check_instance_transforms(const Context& context, const Geometry& triangle)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float3 scale : {float3{0, 1, 1}, float3{1, -1, 1}, float3{1, 1, infinity}, float3{nan, 1, 1}}) {
        const std::array instances{Instance{.geometry = triangle, .scale = scale}};
        expect_error<std::invalid_argument>([&] { Scene invalid(context, instances); });
    }
    for (float3 axis : {float3{0, 0, 0}, float3{infinity, 0, 0}, float3{0, nan, 1}}) {
        const std::array instances{Instance{.geometry = triangle, .rotation_axis = axis}};
        expect_error<std::invalid_argument>([&] { Scene invalid(context, instances); });
    }
    for (float angle : {infinity, nan}) {
        const std::array instances{Instance{.geometry = triangle, .rotation_degrees = angle}};
        expect_error<std::invalid_argument>([&] { Scene invalid(context, instances); });
    }

    const std::array instances{
        Instance{.geometry = triangle, .translation = {3, 1, 0.5f},
                 .rotation_axis = {0, 0, 5}, .rotation_degrees = 450.0f, .scale = {2, 0.5f, 1.5f}},
        Instance{.geometry = triangle, .translation = {-3, 0, 0.25f},
                 .rotation_axis = {0, 2, 0}, .rotation_degrees = 90.0f, .scale = {0.5f, 2, 1.25f}},
        Instance{.geometry = triangle, .translation = {0, -3, 0}, .rotation_degrees = -90.0f},
    };
    Scene scene(context, instances);
    scene.build();

    hiprtRay ray{};
    ray.minT = 0.001f;
    ray.maxT = 100.0f;
    ray.origin = {3, 1.5f, 2.5f};
    ray.direction = {0, 0, -1};
    const auto combined = scene.trace(ray);
    require(combined && combined->instanceID == 0 && combined->primID == 0 &&
            std::abs(combined->t - 2.0f) < 0.00001f &&
            std::abs(combined->uv.x - 0.375f) < 0.00001f &&
            std::abs(combined->uv.y - 0.5f) < 0.00001f,
            "Expected scale, then normalized-axis rotation, then translation about the local origin.");

    ray.origin = {-1, 0, 0.25f};
    ray.direction = {-1, 0, 0};
    const auto turned = scene.trace(ray);
    require(turned && turned->instanceID == 1 && turned->primID == 0 &&
            std::abs(turned->t - 2.0f) < 0.00001f &&
            std::abs(turned->uv.x - 0.25f) < 0.00001f &&
            std::abs(turned->uv.y - 0.5f) < 0.00001f,
            "Rotation around Y did not turn the triangle plane.");

    ray.origin = {0, -3.25f, 2};
    ray.direction = {0, 0, -1};
    const auto clockwise = scene.trace(ray);
    require(clockwise && clockwise->instanceID == 2 && clockwise->primID == 0 &&
            std::abs(clockwise->t - 2.0f) < 0.00001f &&
            std::abs(clockwise->uv.x - 0.375f) < 0.00001f &&
            std::abs(clockwise->uv.y - 0.5f) < 0.00001f,
            "Negative rotation angle did not rotate clockwise around Z.");
}

std::vector<unsigned char> read_pixels(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Cannot open PNG for reading.");
    const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>(file), {}};
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    const std::unique_ptr<png_image, decltype(&png_image_free)> cleanup(&image, png_image_free);
    require(png_image_begin_read_from_memory(&image, encoded.data(), encoded.size()) != 0, image.message);
    require(image.width == 17 && image.height == 19, "Incorrect PNG dimensions.");
    image.format = PNG_FORMAT_RGB;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    require(png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0, image.message);
    return pixels;
}

}

int main(int argc, char** argv)
{
    try {
        require(argc == 3, "Expected an output image path and PLY fixture directory.");
        const std::filesystem::path output(argv[1]);
        Context context;
        check_ply_import(context, argv[2]);
        check_camera();
        check_dielectric_optics();
        check_path_scattering();
        check_shared_mesh_and_spheres(context);
        expect_error<std::invalid_argument>([] { DirectionalLight light({0, 0, 0}); });
        expect_error<std::invalid_argument>([] {
            DirectionalLight light({std::numeric_limits<float>::infinity(), 0, 1});
        });
        expect_error<std::invalid_argument>([] {
            PointLight light({0, std::numeric_limits<float>::infinity(), 1});
        });
        for (float value : {-1.0f, std::numeric_limits<float>::quiet_NaN()}) {
            expect_error<std::invalid_argument>([&] { PointLight light({0, 0, 2}, {1, 1, 1}, value); });
            expect_error<std::invalid_argument>([&] { PointLight light({0, 0, 2}, {value, 1, 1}); });
        }
        expect_error<std::invalid_argument>([] { PointLight light({0, 0, 2}, {1, 1.1f, 1}); });
        expect_error<std::invalid_argument>([] { Renderer renderer(0, 19); });
        expect_error<std::invalid_argument>([] { Renderer renderer(17, -1); });
        expect_error<std::invalid_argument>([] { Renderer renderer(17, 19, -1); });
        expect_error<std::invalid_argument>([] { Renderer renderer(17, 19, maximum_ray_depth + 1); });
        expect_error<std::invalid_argument>([] {
            Renderer renderer(17, 19, 3, 1, static_cast<Integrator>(-1));
        });
        for (int samples : {0, -1}) {
            expect_error<std::invalid_argument>([&] { Renderer renderer(17, 19, 3, samples); });
        }

        Renderer renderer(17, 19);
        expect_error<std::logic_error>([&] { renderer.save(output); });

        std::array<float3, 3> vertices{{
            {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        }};
        Geometry triangle(context, vertices);
        triangle.build();
        check_antialiasing(context, triangle, output);
        check_unit_plane(context);
        check_reflections(context, triangle, output);
        check_glass(context, triangle, output);
        check_path_transport(context, triangle, output);
        check_area_lights(context, output);
        for (float value : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
            const std::array invalid{Instance{.geometry = triangle, .material = {.reflectivity = value}}};
            expect_error<std::invalid_argument>([&] { Scene scene(context, invalid); });
            const std::array invalid_transmission{
                Instance{.geometry = triangle, .material = {.transmission = value}}};
            expect_error<std::invalid_argument>([&] { Scene scene(context, invalid_transmission); });
        }
        for (float value : {0.9f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
            const std::array invalid{Instance{.geometry = triangle, .material = {.ior = value}}};
            expect_error<std::invalid_argument>([&] { Scene scene(context, invalid); });
        }
        check_box_and_torus(context, triangle, renderer, output);
        check_shared_materials(context, triangle, renderer, output);
        check_scene(context, triangle);
        check_instance_transforms(context, triangle);
        const std::array bad_colors{Instance{.geometry = triangle, .material = {.base_color = {-1, 0, 0}}}};
        expect_error<std::invalid_argument>([&] { Scene invalid(context, bad_colors); });
        const std::array identity_instances{
            Instance{.geometry = triangle, .material = {.base_color = {0.8f, 0.4f, 0.2f}}},
        };
        Scene scene(context, identity_instances);
        scene.build();
        const Camera camera({0, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
        check_sphere(context, triangle, camera, renderer, output);
        const auto timing = renderer.render(scene, camera);
        require(std::isfinite(timing.gpu_ms) && timing.gpu_ms > 0 &&
                std::isfinite(timing.download_ms) && timing.download_ms >= 0 &&
                std::isfinite(timing.total_ms) && timing.total_ms >= timing.download_ms,
                "Rendering must return separate finite GPU, download and total timings.");
        renderer.save(output);
        const auto first = read_pixels(output);
        for (int y = 0; y < 19; ++y) {
            for (int x = 0; x < 17; ++x) {
                const float sx = (2.0f * (x + 0.5f) / 17 - 1.0f) * (17.0f / 19) * 1.5f;
                const float sy = (1.0f - 2.0f * (y + 0.5f) / 19) * 1.5f;
                const float v = (sy + 1.0f) / 2.0f;
                const float u = (sx + 1.0f - v) / 2.0f;
                const float w = 1.0f - u - v;
                const bool hit = u >= 0 && v >= 0 && w >= 0;
                const std::array<int, 3> expected = hit
                    ? std::array<int, 3>{204, 102, 51}
                    : std::array<int, 3>{12, 16, 24};
                for (int channel = 0; channel < 3; ++channel) {
                    const int actual = first[3 * (y * 17 + x) + channel];
                    require(std::abs(actual - expected[channel]) <= (hit ? 1 : 0),
                            "PNG pixels do not match Lambert colors.");
                }
            }
        }

        const DirectionalLight behind({0, 0, -1});
        renderer.render(scene, camera, behind);
        renderer.save(output);
        const auto dark = read_pixels(output);
        const std::size_t center = 3 * (9 * 17 + 8);
        require(dark[center] == 0 && dark[center + 1] == 0 && dark[center + 2] == 0,
                "Back lighting should produce zero diffuse brightness.");

        // A sloped triangle plus nonuniform scale distinguishes inverse transpose from vector transforms.
        const std::array<float3, 3> sloped_vertices{{
            {-1, -1, 1}, {1, -1, -1}, {0, 1, 0},
        }};
        Geometry sloped(context, sloped_vertices);
        sloped.build();
        const std::array sloped_instances{
            Instance{.geometry = sloped, .rotation_axis = {0, 1, 0}, .rotation_degrees = 30.0f,
                     .scale = {2, 1, 0.5f}, .material = {.base_color = {1, 0.5f, 0.25f}}},
        };
        Scene tilted(context, sloped_instances);
        tilted.build();
        const DirectionalLight front({0, 0, 7});
        renderer.render(tilted, camera, front);
        renderer.save(output);
        const auto lit = read_pixels(output);
        // Local normal (1,0,1) becomes (0.5,0,2), then rotates 30 degrees around Y.
        const float brightness = (std::sqrt(3.0f) - 0.25f) / std::sqrt(4.25f);
        const std::array<float, 3> base{1, 0.5f, 0.25f};
        for (int c = 0; c < 3; ++c) {
            require(std::abs(static_cast<int>(lit[center + c]) -
                             static_cast<int>(255 * base[c] * brightness)) <= 1,
                    "Rotated nonuniformly scaled normal produced incorrect Lambert shading.");
        }

        const DirectionalLight diagonal({1, 0, 1});
        // The small blocker is outside the center camera ray, but on its path to the light.
        const std::array shadow_instances{
            Instance{.geometry = triangle, .material = {.base_color = {0.8f, 0.4f, 0.2f}}},
            Instance{.geometry = triangle, .translation = {1, 0, 1}, .scale = {0.25f, 0.25f, 1}},
        };
        Scene shadow_scene(context, shadow_instances);
        shadow_scene.build();
        renderer.render(shadow_scene, camera, diagonal);
        renderer.save(output);
        const auto shadow = read_pixels(output);
        require(shadow[center] == 0 && shadow[center + 1] == 0 && shadow[center + 2] == 0,
                "Any-hit shadow ray failed to detect a blocker off the camera ray.");

        renderer.render(scene, camera, diagonal);
        renderer.save(output);
        const auto unblocked = read_pixels(output);
        const std::array<int, 3> expected_diffuse{144, 72, 36};
        for (int c = 0; c < 3; ++c) {
            require(std::abs(static_cast<int>(unblocked[center + c]) - expected_diffuse[c]) <= 1,
                    "Unblocked surface self-shadowed or lost its cosine lighting.");
        }

        for (float distance : {2.0f, 4.0f}) {
            const PointLight point({0, 0, distance}, {1, 0.5f, 0.25f}, 2.0f);
            renderer.render(scene, camera, point);
            renderer.save(output);
            const auto pixels = read_pixels(output);
            const std::array<float, 3> tinted_base{0.8f, 0.2f, 0.05f};
            for (int c = 0; c < 3; ++c) {
                const int expected = static_cast<int>(255 * tinted_base[c] * 2 / (distance * distance));
                require(std::abs(static_cast<int>(pixels[center + c]) - expected) <= 1,
                        "Point light color, intensity or inverse-square falloff is incorrect.");
            }
            // An off-center pixel needs its own direction, distance and Lambert cosine.
            const float x = 2.0f * (2.0f * 9.5f / 17 - 1.0f) * (17.0f / 19) * 0.75f;
            const float squared = distance * distance + x * x;
            const int expected = static_cast<int>(255 * 0.8f * 2 * distance / (squared * std::sqrt(squared)));
            require(std::abs(static_cast<int>(pixels[center + 3]) - expected) <= 1,
                    "Point light direction or distance was not evaluated at the hit position.");
        }
        renderer.render(shadow_scene, camera, PointLight({2, 0, 2}));
        renderer.save(output);
        require(read_pixels(output)[center] == 0, "Point light missed a blocker before the light.");
        const PointLight short_light({0.5f, 0, 0.5f}, {1, 1, 1}, 0.25f);
        renderer.render(scene, camera, short_light);
        renderer.save(output);
        const auto short_unblocked = read_pixels(output);
        renderer.render(shadow_scene, camera, short_light);
        renderer.save(output);
        require(short_unblocked[center] > 0 && read_pixels(output)[center] == short_unblocked[center],
                "Geometry beyond the point light incorrectly blocked its finite shadow ray.");
        for (const auto& point : {PointLight({0, 0, -2}), PointLight({0, 0, 2}, {1, 1, 1}, 0),
                                  PointLight({0, 0, 0})}) {
            renderer.render(scene, camera, point);
            renderer.save(output);
            require(read_pixels(output)[center] == 0,
                    "Back-facing, zero-intensity or coincident point light should leave the hit unlit.");
        }

        for (float strength : {-1.0f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
            const std::array invalid{Instance{.geometry = triangle, .material = {.specular_strength = strength}}};
            expect_error<std::invalid_argument>([&] { Scene bad(context, invalid); });
        }
        for (float exponent : {0.0f, -1.0f, std::numeric_limits<float>::infinity()}) {
            const std::array invalid{Instance{.geometry = triangle, .material = {.shininess = exponent}}};
            expect_error<std::invalid_argument>([&] { Scene bad(context, invalid); });
        }
        for (float exponent : {2.0f, 8.0f}) {
            const std::array shiny_instances{
                Instance{.geometry = triangle, .material = {.base_color = {0, 0, 0},
                         .specular_strength = 0.8f, .shininess = exponent}},
            };
            Scene shiny(context, shiny_instances);
            shiny.build();
            renderer.render(shiny, camera, front);
            renderer.save(output);
            const auto peak = read_pixels(output);
            require(std::abs(static_cast<int>(peak[center]) - 204) <= 1,
                    "Aligned Phong highlight has incorrect strength.");
            renderer.render(shiny, camera, PointLight({0, 0, 2}, {0.5f, 1, 0.25f}, 2));
            renderer.save(output);
            const auto point_peak = read_pixels(output);
            const std::array<int, 3> point_expected{51, 102, 25};
            for (int c = 0; c < 3; ++c) {
                require(std::abs(static_cast<int>(point_peak[center + c]) - point_expected[c]) <= 1,
                        "Point light failed to tint and attenuate the Phong highlight.");
            }
            renderer.render(shiny, camera, diagonal);
            renderer.save(output);
            const auto off_axis = read_pixels(output);
            const int expected = static_cast<int>(255.0f * 0.8f *
                std::pow(1.0f / std::sqrt(2.0f), exponent));
            for (int c = 0; c < 3; ++c) {
                require(std::abs(static_cast<int>(off_axis[center + c]) - expected) <= 1,
                        "Phong reflection angle or shininess exponent is incorrect.");
            }
            renderer.render(shiny, camera, behind);
            renderer.save(output);
            require(read_pixels(output)[center] == 0, "Back lighting produced a specular highlight.");

            const Camera shifted({0.5f, 0, 2}, {0, 0, 0}, {0, 1, 0}, 73.7397953f);
            renderer.render(shiny, shifted, front);
            renderer.save(output);
            const int shifted_expected = static_cast<int>(255.0f * 0.8f *
                std::pow(2.0f / std::sqrt(4.25f), exponent));
            require(std::abs(static_cast<int>(read_pixels(output)[center]) - shifted_expected) <= 1,
                    "Phong highlight did not follow the camera view direction.");

            const std::array blocked_instances{
                shiny_instances[0],
                Instance{.geometry = triangle, .translation = {1, 0, 1}, .scale = {0.25f, 0.25f, 1}},
            };
            Scene blocked(context, blocked_instances);
            blocked.build();
            renderer.render(blocked, camera, diagonal);
            renderer.save(output);
            require(read_pixels(output)[center] == 0, "Shadow did not suppress the Phong highlight.");
        }

        const std::array behind_instances{
            Instance{.geometry = triangle, .material = {.base_color = {0.8f, 0.4f, 0.2f}}},
            Instance{.geometry = triangle, .translation = {-1, 0, -1}, .scale = {0.25f, 0.25f, 1}},
        };
        Scene behind_scene(context, behind_instances);
        behind_scene.build();
        renderer.render(behind_scene, camera, diagonal);
        renderer.save(output);
        require(read_pixels(output)[center] == unblocked[center],
                "An object opposite the light incorrectly cast a shadow.");

        const std::array distant_instances{
            Instance{.geometry = triangle, .material = {.base_color = {0.8f, 0.4f, 0.2f}}},
            Instance{.geometry = triangle, .translation = {150, 0, 150}, .scale = {2, 2, 1}},
        };
        Scene distant_scene(context, distant_instances);
        distant_scene.build();
        renderer.render(distant_scene, camera, diagonal);
        renderer.save(output);
        require(read_pixels(output)[center] == 0,
                "Directional-light shadow ray was incorrectly limited to the camera far distance.");

        const Camera away({10, 0, 2}, {10, 0, 0}, {0, 1, 0}, 73.7397953f);
        renderer.render(scene, away);
        renderer.save(output);
        const auto second = read_pixels(output);
        for (std::size_t i = 0; i < second.size(); i += 3) {
            require(second[i] == 12 && second[i + 1] == 16 && second[i + 2] == 24,
                    "Repeated rendering left stale pixels or missed a partial block.");
        }

        Geometry unbuilt_geometry(context, vertices);
        const std::array unbuilt_instances{Instance{unbuilt_geometry}};
        expect_error<std::logic_error>([&] { Scene invalid(context, unbuilt_instances); });
        Scene unbuilt(context, identity_instances);
        expect_error<std::logic_error>([&] { renderer.render(unbuilt, camera); });
        expect_error<std::logic_error>([&] { renderer.save(output); });
        std::filesystem::remove(output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
