#include "gpu/callbacks.h"
#include "gpu/scene.h"
#include "gpu/device.h"
#include "rendering/renderer.h"
#include "reference_scene.h"
#include "analytic_intersection_tests.h"

using namespace hip_demo;

namespace {

__device__ bool hit_direct_objects(const Ray& ray, Hit& closest_hit) {
    bool found = false;
    float closest_distance = 1.0e30f;
    for (unsigned int index = 0; index < scene_object_count; ++index) {
        Hit candidate{};
        if (hit_primitive(ray, index, closest_distance, candidate)) {
            closest_hit = candidate;
            closest_distance = candidate.distance;
            found = true;
        }
    }
    return found;
}

constexpr unsigned int mesh_special_check_count = 64;
constexpr unsigned int mesh_check_count = mesh_special_check_count + 64 * 48;

__device__ float3 mesh_vertex(const float4* vertices, unsigned int index) {
    const float4 vertex = vertices[index];
    return {vertex.x, vertex.y, vertex.z};
}

// Independent reference for the mesh intersection checks.
__device__ bool hit_reference_mesh(const Ray& ray, const float4* vertices, unsigned int triangle_count,
                                   float maximum_distance,
                                   float& distance, unsigned int& primitive, float3& normal) {
    bool found = false;
    for (unsigned int index = 0; index < triangle_count; ++index) {
        const float3 a = mesh_vertex(vertices, index * 3);
        const float3 edge1 = mesh_vertex(vertices, index * 3 + 1) - a;
        const float3 edge2 = mesh_vertex(vertices, index * 3 + 2) - a;
        const float3 p = cross(ray.direction, edge2);
        const float determinant = dot(edge1, p);
        if (fabsf(determinant) < 1.0e-8f) {
            continue;
        }
        const float inverse = 1.0f / determinant;
        const float3 offset = ray.origin - a;
        const float u = dot(offset, p) * inverse;
        const float3 q = cross(offset, edge1);
        const float v = dot(ray.direction, q) * inverse;
        const float t = dot(edge2, q) * inverse;
        if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f && t >= 0.001f && t < maximum_distance) {
            maximum_distance = t;
            distance = t;
            primitive = index;
            normal = normalize(cross(edge1, edge2));
            found = true;
        }
    }
    return found;
}

__global__ void check_mesh(TraceScene scene, unsigned int* failures) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= mesh_check_count) {
        return;
    }
    Ray ray{};
    float maximum_distance = 1.0e30f;
    if (index < mesh_special_check_count) {
        const unsigned int triangle = index % mesh_triangles.size();
        const float3 a = mesh_vertex(scene.mesh_vertices, triangle * 3);
        const float3 b = mesh_vertex(scene.mesh_vertices, triangle * 3 + 1);
        const float3 c = mesh_vertex(scene.mesh_vertices, triangle * 3 + 2);
        const float3 normal = normalize(cross(b - a, c - a));
        const float3 center = (a + b + c) / 3.0f;
        ray = index >= 48 && index < 56 ? Ray{center - 0.1f * normal, normal} :
            Ray{center + 0.25f * normal, -normal};
        if (index < 48) {
            const unsigned int limit = index / mesh_triangles.size();
            maximum_distance = limit == 0 ? 0.26f : limit == 1 ? 0.35f :
                limit == 2 ? 0.5f : limit == 3 ? 1.0f : limit == 4 ? 2.0f : 1.0e30f;
        } else if (index >= 56) {
            maximum_distance = 0.2f;
        }
    } else {
        const unsigned int pixel = index - mesh_special_check_count;
        const float3 forward = normalize(camera_target - camera_position);
        const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const float3 up = cross(right, forward);
        const float u = (2.0f * (static_cast<float>(pixel % 64) + 0.5f) / 64 - 1.0f) *
            (static_cast<float>(image_width) / image_height) * 0.41421356f;
        const float v = (1.0f - 2.0f * (static_cast<float>(pixel / 64) + 0.5f) / 48) *
            0.41421356f;
        ray = {camera_position, normalize(forward + u * right + v * up)};
    }
    float expected_distance = 0.0f;
    unsigned int expected_primitive = 0;
    float3 expected_normal{};
    const bool expected = hit_reference_mesh(ray, scene.mesh_vertices, scene.mesh_triangle_count, maximum_distance,
                                              expected_distance, expected_primitive, expected_normal);
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = maximum_distance;
    hiprtGeomTraversalClosest traversal(scene.mesh, traversal_ray);
    const hiprtHit actual = traversal.getNextHit();
    unsigned int failed = 0;
    if ((index < 48 && (!expected || expected_primitive != index % mesh_triangles.size() ||
                        fabsf(expected_distance - 0.25f) > 0.0001f)) ||
        (index >= 48 && index < 56 &&
         (!expected || expected_primitive != index % mesh_triangles.size() ||
          fabsf(expected_distance - 0.1f) > 0.0001f)) ||
        (index >= 56 && index < 64 && expected) ||
        expected != actual.hasHit() ||
        (expected && (actual.primID != expected_primitive || !isfinite(actual.t) ||
                      fabsf(actual.t - expected_distance) > 0.0001f ||
                      !(dot(normalize(actual.normal), expected_normal) > 0.999f)))) {
        failed |= 1;
    }
    if (index < 56 || index >= mesh_special_check_count) {
        Hit custom{}, combined{};
        const bool custom_hit = hit_custom_objects(ray, custom, scene);
        const bool combined_hit = hit_objects(ray, combined, scene);
        const bool mesh_closest = expected && (!custom_hit || expected_distance < custom.distance);
        const bool front_face = dot(ray.direction, expected_normal) < 0.0f;
        const bool crystal = expected_primitive < mesh_triangles.size();
        const Material material = crystal ? Material::phong : Material::diffuse;
        if (combined_hit != (expected || custom_hit) ||
            (mesh_closest && (combined.object_id != scene_object_count + (crystal ? 0 : 1) ||
                              combined.material != material ||
                              combined.front_face != front_face ||
                              !(fabsf(dot(combined.normal, expected_normal)) > 0.999f) ||
                              fabsf(combined.distance - expected_distance) > 0.0001f)) ||
            (custom_hit && !mesh_closest && (combined.object_id != custom.object_id ||
                                             fabsf(combined.distance - custom.distance) > 0.0001f))) {
            failed |= 2;
        }
    }
    failures[index] = failed;
}
struct IntersectionCheck {
    bool expected_hit;
    bool actual_hit;
    float expected_distance;
    float actual_distance;
    Material expected_material;
    Material actual_material;
    bool expected_front_face;
    bool actual_front_face;
    unsigned int expected_object_id;
    unsigned int actual_object_id;
    float3 expected_normal;
    float3 actual_normal;
};

constexpr unsigned int special_ray_count = scene_object_count + 4;
constexpr unsigned int intersection_check_count = special_ray_count + 64 * 48;

__global__ void check_intersections(TraceScene scene, IntersectionCheck* results) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= intersection_check_count) {
        return;
    }
    Ray ray{};
    if (index < scene_object_count) {
        const SceneObject object = scene_object(index);
        if (object.sphere) {
            ray = {object.center + float3{0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
        } else {
            const float3 normal{0.0f, object.tilt_cosine, object.tilt_sine};
            ray = {object.center + float3{torus_major_radius, 0.0f, 0.0f} + 0.4f * normal, -normal};
        }
    } else if (index == scene_object_count) {
        ray = {{0.0f, 10.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    } else if (index == scene_object_count + 1) {
        ray = {scene_object(0).center, {1.0f, 0.0f, 0.0f}};
    } else if (index == scene_object_count + 2) {
        ray = {scene_object(1).center + float3{torus_major_radius, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    } else if (index == scene_object_count + 3) {
        ray = {scene_object(0).center - float3{2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    } else {
        const unsigned int pixel = index - special_ray_count;
        const float3 forward = normalize(camera_target - camera_position);
        const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const float3 up = cross(right, forward);
        const float u = (2.0f * (static_cast<float>(pixel % 64) + 0.5f) / 64 - 1.0f) *
            (static_cast<float>(image_width) / image_height) * 0.41421356f;
        const float v = (1.0f - 2.0f * (static_cast<float>(pixel / 64) + 0.5f) / 48) *
            0.41421356f;
        ray = {camera_position, normalize(forward + u * right + v * up)};
    }
    Hit expected{}, actual{};
    const bool expected_hit = hit_direct_objects(ray, expected);
    const bool actual_hit = hit_custom_objects(ray, actual, scene);
    results[index] = {expected_hit, actual_hit, expected.distance, actual.distance,
                      expected.material, actual.material, expected.front_face, actual.front_face,
                      expected.object_id, actual.object_id, expected.normal, actual.normal};
}

__global__ void check_adaptive_sampling(unsigned int* failures) {
    unsigned int failed = 0;
    AdaptivePixel stable{};
    for (int i = 0; i < adaptive_min_samples - 1; ++i) {
        add_adaptive_sample(stable, {1.0f, 0.5f, 0.25f});
    }
    if (adaptive_converged(stable)) {
        failed |= 1;
    }
    add_adaptive_sample(stable, {1.0f, 0.5f, 0.25f});
    if (!adaptive_converged(stable)) {
        failed |= 2;
    }
    stable.specular = true;
    if (adaptive_converged(stable)) {
        failed |= 4;
    }
    while (stable.count < adaptive_specular_min_samples) {
        add_adaptive_sample(stable, {1.0f, 0.5f, 0.25f});
    }
    if (!adaptive_converged(stable)) {
        failed |= 8;
    }
    add_adaptive_sample(stable, {20.0f, 0.5f, 0.25f});
    if (adaptive_converged(stable)) {
        failed |= 16;
    }
    SceneSampling custom;
    custom.minimum = 1;
    AdaptivePixel single{};
    add_adaptive_sample(single, {1, 1, 1});
    if (adaptive_converged(single, &custom)) failed |= 32;
    add_adaptive_sample(single, {1, 1, 1});
    if (!adaptive_converged(single, &custom)) failed |= 64;
    *failures = failed;
}

__global__ void check_cornell_scene(TraceScene scene, unsigned int* failures) {
    unsigned int failed = 0;
    Hit hit{};
    const float3 origin{0.0f, 2.0f, 0.0f};
    if (!hit_room({origin, {0.0f, -1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::diffuse || fabsf(hit.distance - 2.0f) > 0.0001f ||
        hit_room({origin, {0.0f, -1.0f, 0.0f}}, 1.9f, hit)) {
        failed |= 1;
    }
    if (!hit_room({origin, {-1.0f, 0.0f, 0.0f}}, 1.0e30f, hit) || hit.albedo.x <= hit.albedo.y ||
        !hit_room({origin, {1.0f, 0.0f, 0.0f}}, 1.0e30f, hit) || hit.albedo.y <= hit.albedo.x ||
        !hit_room({origin, {0.0f, 0.0f, -1.0f}}, 1.0e30f, hit) || hit.material != Material::diffuse) {
        failed |= 2;
    }
    if (!hit_room({origin, {0.0f, 1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::emissive || !hit.front_face ||
        !hit_room({{2.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::diffuse ||
        !hit_room({{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::emissive || hit.front_face ||
        hit_room({origin, {0.0f, 0.0f, 1.0f}}, 1.0e30f, hit)) {
        failed |= 4;
    }
    if (!hit_scene({{-1.65f, 2.0f, -0.9f}, {0.0f, -1.0f, 0.0f}}, hit, scene) ||
        hit.object_id != 0 || hit.material != Material::metal) {
        failed |= 8;
    }
    Random marking_random{123456789u};
    for (const unsigned int index : {0u, 2u, 6u}) {
        const float3 center = scene_object(index).center;
        const Ray ray{{center.x, 2.0f, center.z}, {0.0f, -1.0f, 0.0f}};
        bool specular = false;
        trace(ray, marking_random, scene, &specular);
        if (!specular) {
            failed |= 256;
        }
    }
    bool diffuse_primary = false;
    trace({origin, {0.0f, 0.0f, -1.0f}}, marking_random, scene, &diffuse_primary);
    if (diffuse_primary) {
        failed |= 256;
    }
    Random random{123456789u};
    const float3 illuminated = trace({origin, {0.0f, 0.0f, -1.0f}}, random, scene);
    if (!(illuminated.x > 0.0f && illuminated.y > 0.0f && illuminated.z > 0.0f)) {
        failed |= 8;
    }
    float cosine_sum = 0.0f;
    for (int i = 0; i < 2048; ++i) {
        const float3 direction = sample_phong_lobe({0.0f, 1.0f, 0.0f}, random);
        if (!(direction.y >= 0.0f) || fabsf(dot(direction, direction) - 1.0f) > 0.0001f) {
            failed |= 16;
        }
        cosine_sum += direction.y;
    }
    const float expected_cosine = (phong_exponent + 1.0f) / (phong_exponent + 2.0f);
    if (fabsf(cosine_sum / 2048.0f - expected_cosine) > 0.005f) {
        failed |= 16;
    }
    const float3 forward = normalize(camera_target - camera_position);
    const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const float3 up = cross(right, forward);
    for (int x = 1650; x <= 1670; x += 10) {
        const float u = (2.0f * (x + 0.5f) / image_width - 1.0f) *
            (static_cast<float>(image_width) / image_height) * 0.41421356f;
        const float v = (1.0f - 2.0f * 1520.5f / image_height) * 0.41421356f;
        const Ray camera_ray{camera_position, normalize(forward + u * right + v * up)};
        Hit sphere{}, reflected_hit{};
        if (!hit_scene(camera_ray, sphere, scene) || sphere.object_id != 4 ||
            sphere.material != Material::metal || sphere.roughness != 0.0f) {
            failed |= 32;
            continue;
        }
        const Ray reflected_ray{sphere.point + sphere.normal * 0.002f,
                                reflect(camera_ray.direction, sphere.normal)};
        if (!hit_scene(reflected_ray, reflected_hit, scene) ||
            reflected_hit.object_id != scene_object_count) {
            failed |= 32;
        }
    }
    const Ray glass_ray{camera_position,
                        normalize(scene_object(6).center + float3{0.15f, 0.12f, 0.0f} - camera_position)};
    Hit entry{}, exit{}, behind{};
    float3 inside_direction{}, outside_direction{};
    if (!hit_scene(glass_ray, entry, scene) || entry.object_id != 6 ||
        entry.material != Material::glass || !entry.front_face ||
        !refract(glass_ray.direction, entry.normal, 1.0f / 1.5f, inside_direction) ||
        !hit_scene({entry.point - entry.normal * 0.002f, inside_direction}, exit, scene) ||
        exit.object_id != 6 || exit.front_face ||
        !refract(inside_direction, exit.normal, 1.5f, outside_direction) ||
        dot(glass_ray.direction, outside_direction) > 0.999f ||
        !hit_scene({exit.point - exit.normal * 0.002f, outside_direction}, behind, scene)) {
        failed |= 64;
    }
    constexpr std::array<float3, 4> cat_targets{{
        {cat_position.x, 0.55f, cat_position.z},
        {cat_position.x, 1.20f, cat_position.z},
        {cat_position.x + 0.23f, 1.55f, cat_position.z},
        {cat_position.x + 0.63f, 0.59f, cat_position.z},
    }};
    for (const float3 target : cat_targets) {
        const Ray ray{camera_position, normalize(target - camera_position)};
        Hit cat_hit{};
        if (!hit_scene(ray, cat_hit, scene) || cat_hit.object_id != scene_object_count + 1 ||
            cat_hit.material != Material::diffuse || cat_hit.albedo.x < 0.5f) {
            failed |= 128;
        }
    }
    for (const float offset : {-0.13f, 0.13f}) {
        const float3 target{cat_position.x + offset, 1.25f, cat_position.z + 0.25f};
        const Ray ray{camera_position, normalize(target - camera_position)};
        Hit eye{};
        if (!hit_scene(ray, eye, scene) || eye.object_id != scene_object_count + 1 ||
            eye.material != Material::diffuse || eye.albedo.x > 0.1f) {
            failed |= 128;
        }
    }
    *failures = failed;
}

void check_adaptive_render(TraceScene scene) {
    constexpr int width = 17;
    constexpr int height = 13;
    constexpr std::size_t count = width * height;
    auto adaptive_pixels = allocate_device<std::uint32_t>(count, "Allocating adaptive test pixels");
    auto fixed_pixels = allocate_device<std::uint32_t>(count, "Allocating fixed test pixels");
    auto states = allocate_device<AdaptivePixel>(count, "Allocating adaptive test state");
    auto sums = allocate_device<float3>(count, "Allocating fixed test sums");
    auto device_statistics = allocate_device<SampleStatistics>(1, "Allocating test statistics");
    std::array<std::uint32_t, count> fixed_at_64{};
    for (const int cap : {1, 63, 64, 73}) {
        for (int first = 0; first < cap;) {
            const int end = first + std::min(samples_per_batch, cap - first);
            hipLaunchKernelGGL(render_adaptive_kernel, dim3(3, 2), dim3(8, 8), 0, 0,
                               adaptive_pixels.get(), states.get(), scene, width, height, first, end, cap);
            check_hip(hipGetLastError(), "Launching adaptive test renderer");
            hipLaunchKernelGGL(render_kernel, dim3(3, 2), dim3(8, 8), 0, 0,
                               fixed_pixels.get(), sums.get(), scene, width, height, first, end, cap);
            check_hip(hipGetLastError(), "Launching fixed test renderer");
            first = end;
        }
        std::array<AdaptivePixel, count> actual_states{};
        std::array<std::uint32_t, count> actual_pixels{}, expected_pixels{};
        check_hip(hipMemcpy(actual_states.data(), states.get(), sizeof(actual_states), hipMemcpyDeviceToHost),
                  "Reading adaptive test state");
        check_hip(hipMemcpy(actual_pixels.data(), adaptive_pixels.get(), sizeof(actual_pixels), hipMemcpyDeviceToHost),
                  "Reading adaptive test pixels");
        check_hip(hipMemcpy(expected_pixels.data(), fixed_pixels.get(), sizeof(expected_pixels), hipMemcpyDeviceToHost),
                  "Reading fixed test pixels");
        SampleStatistics expected{0, 0xffffffffu, 0};
        bool saw_early = false, saw_cap = false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& pixel = actual_states[i];
            if (!pixel.done || pixel.count < static_cast<unsigned int>(std::min(cap, adaptive_min_samples)) ||
                pixel.count > static_cast<unsigned int>(cap) ||
                (pixel.count == static_cast<unsigned int>(cap) && actual_pixels[i] != expected_pixels[i]) ||
                (cap == 73 && pixel.count == 64 && actual_pixels[i] != fixed_at_64[i])) {
                throw std::runtime_error("Adaptive render check failed at cap " + std::to_string(cap) +
                                         ", pixel " + std::to_string(i));
            }
            saw_early = saw_early || pixel.count < static_cast<unsigned int>(cap);
            saw_cap = saw_cap || pixel.count == static_cast<unsigned int>(cap);
            expected.total += pixel.count;
            expected.minimum = std::min(expected.minimum, pixel.count);
            expected.maximum = std::max(expected.maximum, pixel.count);
        }
        if (cap == 64) {
            fixed_at_64 = expected_pixels;
        }
        if (cap == 73 && (!saw_early || !saw_cap)) {
            throw std::runtime_error("Adaptive render did not exercise both early completion and the sample cap");
        }
        const SampleStatistics initial{0, 0xffffffffu, 0};
        check_hip(hipMemcpy(device_statistics.get(), &initial, sizeof(initial), hipMemcpyHostToDevice),
                  "Initializing test statistics");
        hipLaunchKernelGGL(summarize_samples, dim3(1), dim3(256), 0, 0, states.get(), count, device_statistics.get());
        check_hip(hipGetLastError(), "Launching test statistics");
        SampleStatistics actual{};
        check_hip(hipMemcpy(&actual, device_statistics.get(), sizeof(actual), hipMemcpyDeviceToHost),
                  "Reading test statistics");
        if (actual.total != expected.total || actual.minimum != expected.minimum || actual.maximum != expected.maximum) {
            throw std::runtime_error("Adaptive sample statistics check failed at cap " + std::to_string(cap));
        }
    }
}

void run_checks(TraceScene scene) {
    auto adaptive_failure = allocate_device<unsigned int>(1, "Allocating adaptive sampling checks");
    hipLaunchKernelGGL(check_adaptive_sampling, dim3(1), dim3(1), 0, 0, adaptive_failure.get());
    check_hip(hipGetLastError(), "Launching adaptive sampling checks");
    unsigned int adaptive_result = 0;
    check_hip(hipMemcpy(&adaptive_result, adaptive_failure.get(), sizeof(adaptive_result), hipMemcpyDeviceToHost),
              "Reading adaptive sampling checks");
    if (adaptive_result != 0) {
        throw std::runtime_error("Adaptive sampling checks failed (flags " + std::to_string(adaptive_result) + ")");
    }
    check_adaptive_render(scene);
    std::cout << "Adaptive sampling checks passed.\n";
    auto mesh_failures = allocate_device<unsigned int>(mesh_check_count, "Allocating mesh checks");
    hipLaunchKernelGGL(check_mesh, dim3((mesh_check_count + 127) / 128), dim3(128),
                       0, 0, scene, mesh_failures.get());
    check_hip(hipGetLastError(), "Launching HIP RT mesh checks");
    std::array<unsigned int, mesh_check_count> mesh_results{};
    check_hip(hipMemcpy(mesh_results.data(), mesh_failures.get(), sizeof(mesh_results), hipMemcpyDeviceToHost),
              "Reading HIP RT mesh checks");
    for (std::size_t index = 0; index < mesh_results.size(); ++index) {
        if (mesh_results[index] != 0) {
            throw std::runtime_error("HIP RT mesh intersection check failed for ray " +
                                     std::to_string(index) + " (flags " + std::to_string(mesh_results[index]) + ")");
        }
    }
    std::cout << "HIP RT triangle mesh intersection checks passed (" << scene.mesh_triangle_count
              << " triangles).\n";
    auto device_results = allocate_device<IntersectionCheck>(intersection_check_count, "Allocating intersection checks");
    hipLaunchKernelGGL(check_intersections, dim3((intersection_check_count + 127) / 128),
                       dim3(128), 0, 0, scene, device_results.get());
    check_hip(hipGetLastError(), "Launching HIP RT intersection checks");
    std::array<IntersectionCheck, intersection_check_count> results{};
    check_hip(hipMemcpy(results.data(), device_results.get(), results.size() * sizeof(results[0]), hipMemcpyDeviceToHost),
              "Reading HIP RT intersection checks");
    constexpr std::array<Material, scene_object_count> expected_materials{
        Material::metal, Material::glass, Material::phong,
        Material::phong, Material::metal, Material::diffuse, Material::glass};
    for (std::size_t index = 0; index < results.size(); ++index) {
        const IntersectionCheck& result = results[index];
        if ((index < scene_object_count && (!result.expected_hit || result.expected_object_id != index ||
                                           result.expected_material != expected_materials[index])) ||
            (index == scene_object_count && result.expected_hit) ||
            (index == scene_object_count + 1 &&
             (!result.expected_hit || result.expected_object_id != 0 || result.expected_front_face)) ||
            (index == scene_object_count + 2 &&
             (!result.expected_hit || result.expected_object_id != 1 || result.expected_front_face)) ||
            (index == scene_object_count + 3 && (!result.expected_hit || result.expected_object_id != 0))) {
            throw std::runtime_error("Direct intersection check failed for ray " + std::to_string(index));
        }
        if (index < scene_object_count && !scene_object(static_cast<unsigned int>(index)).sphere) {
            const SceneObject object = scene_object(static_cast<unsigned int>(index));
            const float3 normal{0.0f, object.tilt_cosine, object.tilt_sine};
            if (std::abs(result.expected_distance - (0.4f - torus_minor_radius)) > 0.003f ||
                dot(result.expected_normal, normal) < 0.999f) {
                throw std::runtime_error("Rotated torus intersection check failed for ray " + std::to_string(index));
            }
        }
        const float distance_tolerance = result.expected_hit &&
            !scene_object(result.expected_object_id).sphere ? 0.002f : 0.0001f;
        const float expected_normal_squared = dot(result.expected_normal, result.expected_normal);
        const float actual_normal_squared = dot(result.actual_normal, result.actual_normal);
        const float normal_dot = dot(result.expected_normal, result.actual_normal);
        if (result.expected_hit != result.actual_hit ||
            (result.expected_hit && (!std::isfinite(result.actual_distance) ||
             std::abs(result.expected_distance - result.actual_distance) > distance_tolerance ||
             result.expected_material != result.actual_material ||
             result.expected_object_id != result.actual_object_id ||
             result.expected_front_face != result.actual_front_face ||
             !(expected_normal_squared > 0.0f) || !(actual_normal_squared > 0.0f) ||
             !std::isfinite(actual_normal_squared) || !std::isfinite(normal_dot) ||
             !(normal_dot >= 0.999f * std::sqrt(expected_normal_squared * actual_normal_squared))))) {
            throw std::runtime_error("HIP RT intersection check failed for ray " + std::to_string(index) +
                                     " (expected hit " + std::to_string(result.expected_hit) +
                                     " at " + std::to_string(result.expected_distance) +
                                     ", object " + std::to_string(result.expected_object_id) +
                                     ", got hit " + std::to_string(result.actual_hit) +
                                     " at " + std::to_string(result.actual_distance) +
                                     ", object " + std::to_string(result.actual_object_id) +
                                     ", normal dot " + std::to_string(normal_dot) +
                                     ", front faces " + std::to_string(result.expected_front_face) +
                                     "/" + std::to_string(result.actual_front_face) + ")");
        }
    }
    std::cout << "HIP RT custom primitive intersection checks passed.\n";
    auto device_failures = allocate_device<unsigned int>(1, "Allocating Cornell scene checks");
    hipLaunchKernelGGL(check_cornell_scene, dim3(1), dim3(1), 0, 0, scene, device_failures.get());
    check_hip(hipGetLastError(), "Launching Cornell scene checks");
    unsigned int failures = 0;
    check_hip(hipMemcpy(&failures, device_failures.get(), sizeof(failures), hipMemcpyDeviceToHost),
              "Reading Cornell scene checks");
    if (failures != 0) {
        throw std::runtime_error("Cornell scene checks failed (flags " + std::to_string(failures) + ")");
    }
    std::cout << "Cornell room, lighting, materials, reflections and cat checks passed.\n";
}

__global__ void check_ply_scene(TraceScene scene, unsigned int* failure) {
    const Ray ray{{cat_position.x, 0.30f, 2.30f}, {0.0f, 0.0f, -1.0f}};
    hiprtRay hip_ray{};
    hip_ray.origin = ray.origin;
    hip_ray.direction = ray.direction;
    hip_ray.minT = 0.001f;
    hip_ray.maxT = 10.0f;
    hiprtGeomTraversalClosest traversal(scene.mesh, hip_ray);
    const hiprtHit mesh_hit = traversal.getNextHit();
    Hit hit{};
    *failure = !mesh_hit.hasHit() || mesh_hit.primID != mesh_triangles.size() ||
        !hit_objects(ray, hit, scene) || hit.object_id != scene_object_count + 1 ||
        hit.material != Material::diffuse || fabsf(hit.distance - 0.55f) > 0.001f;
}

__global__ void check_detail_scene(TraceScene scene, unsigned int* failure) {
    unsigned int failed = 0;
    for (unsigned int index = 0; index < 2; ++index) {
        const float3 center = scene_object(index, true).center;
        const Ray ray{center + float3{0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
        Hit hit{};
        const Material expected = index == 0 ? Material::metal : Material::glass;
        if (!hit_custom_objects(ray, hit, scene) || hit.object_id != index || hit.material != expected ||
            fabsf(hit.distance - (1.0f - detail_sphere_radius)) > 0.001f ||
            (index == 0 && hit.roughness != 0.0f)) {
            failed |= 1;
        }
    }
    const Ray torus_ray{{detail_torus_center.x + detail_torus_major_radius,
                         detail_torus_center.y - 0.5f, detail_torus_center.z}, {0.0f, 1.0f, 0.0f}};
    Hit torus{}, ceiling{};
    if (!hit_custom_objects(torus_ray, torus, scene) || torus.object_id != 2 ||
        torus.material != Material::emissive || !torus.front_face ||
        !hit_room({{0.0f, 3.8f, 0.0f}, {0.0f, 1.0f, 0.0f}}, 1.0e30f, ceiling, true) ||
        ceiling.material != Material::diffuse) {
        failed |= 2;
    }
    Random random{123456789u};
    for (int i = 0; i < 128; ++i) {
        const LightSample light = sample_torus_light(random);
        const float radial = sqrtf((light.point.x - detail_torus_center.x) *
                                    (light.point.x - detail_torus_center.x) +
                                    (light.point.z - detail_torus_center.z) *
                                    (light.point.z - detail_torus_center.z));
        if (fabsf(torus_distance(light.point, detail_torus_center,
                                 detail_torus_major_radius, detail_torus_minor_radius)) > 0.0001f ||
            fabsf(dot(light.normal, light.normal) - 1.0f) > 0.0001f ||
            fabsf(light.area_weight - 4.0f * pi * pi * detail_torus_minor_radius * radial) > 0.0001f) {
            failed |= 4;
        }
    }
    float3 illumination{};
    for (int i = 0; i < 128; ++i) {
        illumination = illumination + trace({{2.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, random, scene);
    }
    if (!(illumination.x > 0.0f && illumination.y > 0.0f && illumination.z > 0.0f)) {
        failed |= 8;
    }
    const float3 glass_center = scene_object(1, true).center;
    const Ray glass_ray{detail_camera_position,
                        normalize(glass_center + float3{0.15f, 0.12f, 0.0f} - detail_camera_position)};
    Hit entry{}, exit{}, behind{};
    float3 inside_direction{}, outside_direction{};
    if (!hit_scene(glass_ray, entry, scene) || entry.material != Material::glass || !entry.front_face ||
        !refract(glass_ray.direction, entry.normal, 1.0f / 1.5f, inside_direction) ||
        !hit_scene({entry.point - entry.normal * 0.002f, inside_direction}, exit, scene) ||
        exit.object_id != 1 || exit.front_face ||
        !refract(inside_direction, exit.normal, 1.5f, outside_direction) ||
        dot(glass_ray.direction, outside_direction) > 0.999f ||
        !hit_scene({exit.point - exit.normal * 0.002f, outside_direction}, behind, scene)) {
        failed |= 16;
    }
    bool reflected_light = false;
    const float3 mirror_center = scene_object(0, true).center;
    for (int y = -8; y <= 8 && !reflected_light; ++y) {
        for (int x = -8; x <= 8 && !reflected_light; ++x) {
            const float3 target = mirror_center + float3{x * 0.075f, y * 0.075f, 0.0f};
            const Ray ray{detail_camera_position, normalize(target - detail_camera_position)};
            Hit mirror{}, reflected{};
            if (hit_scene(ray, mirror, scene) && mirror.material == Material::metal && mirror.object_id == 0 &&
                hit_scene({mirror.point + mirror.normal * 0.002f, reflect(ray.direction, mirror.normal)},
                          reflected, scene) && reflected.material == Material::emissive) {
                reflected_light = true;
            }
        }
    }
    if (!reflected_light) {
        failed |= 32;
    }
    Hit detail_mesh{};
    if (!hit_objects({{0.0f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f}}, detail_mesh, scene) ||
        detail_mesh.object_id != detail_object_count + 1 || detail_mesh.material != Material::phong ||
        !(detail_mesh.albedo.z > detail_mesh.albedo.y && detail_mesh.albedo.y > detail_mesh.albedo.x)) {
        failed |= 512;
    }
    Hit lettering{}, lettering_side{}, bare_wall{};
    if (!hit_scene({{-1.52f, 3.0f, -2.0f}, {0, 0, -1}}, lettering, scene) ||
        lettering.object_id != detail_object_count || lettering.material != Material::metal ||
        fabsf(lettering.point.z + 2.3f) > 0.0001f || lettering.normal.z < 0.999f ||
        !hit_scene({{-1.64f, 3.0f, -2.4f}, {1, 0, 0}}, lettering_side, scene) ||
        lettering_side.object_id != detail_object_count || lettering_side.normal.x > -0.999f ||
        !hit_scene({{-2.2f, 3.0f, -2.0f}, {0, 0, -1}}, bare_wall, scene) ||
        bare_wall.material != Material::diffuse || fabsf(bare_wall.point.z + 2.5f) > 0.0001f) {
        failed |= 2048;
    }
    Hit tile{}, grout{}, positive_edge{}, negative_edge{}, corner{}, classic{}, underside{}, grazing{};
    const auto floor_ray = [](float x, float z) {
        return Ray{{x, 1.0f, z}, {0.0f, -1.0f, 0.0f}};
    };
    if (!hit_room(floor_ray(0.375f, 0.375f), 10.0f, tile, true) ||
        !hit_room(floor_ray(0.0f, 0.375f), 10.0f, grout, true) ||
        !hit_room(floor_ray(0.020f, 0.375f), 10.0f, positive_edge, true) ||
        !hit_room(floor_ray(-0.020f, 0.375f), 10.0f, negative_edge, true) ||
        !hit_room(floor_ray(0.020f, 0.020f), 10.0f, corner, true) ||
        !hit_room(floor_ray(0.0f, 0.375f), 10.0f, classic) ||
        !hit_room({{0.020f, -1.0f, 0.375f}, {0.0f, 1.0f, 0.0f}}, 10.0f, underside, true) ||
        !hit_room({{1.020f, 0.010f, 0.375f}, normalize({-1.0f, -0.010f, 0.0f})}, 10.0f, grazing, true) ||
        !tile.tiled_floor || !grout.tiled_floor || classic.tiled_floor ||
        tile.albedo.x <= grout.albedo.x || classic.albedo.x != 0.75f ||
        fabsf(tile.normal.x) > 0.0001f || fabsf(tile.normal.z) > 0.0001f ||
        positive_edge.normal.x >= -0.55f || negative_edge.normal.x <= 0.55f ||
        corner.normal.x >= -0.30f || corner.normal.z >= -0.30f ||
        !positive_edge.front_face || underside.front_face || underside.normal.y >= 0.0f ||
        fabsf(grazing.normal.x) > 0.0001f || grazing.normal.y < 0.999f ||
        fabsf(positive_edge.distance - 1.0f) > 0.0001f) {
        failed |= 64;
    }
    Hit wood{}, wood_repeat{}, marble{}, marble_repeat{}, marble_backside{}, classic_back{};
    const Ray ceiling_ray{{0.4f, 3.8f, 0.2f}, {0.0f, 1.0f, 0.0f}};
    const Ray back_ray{{0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    float wood_minimum = 1.0f, wood_maximum = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        Hit sample{};
        if (!hit_room({{0.4f, 3.8f, -2.0f + i * 0.2f}, {0.0f, 1.0f, 0.0f}},
                      10.0f, sample, true, scene.settings)) {
            failed |= 128;
        }
        wood_minimum = fminf(wood_minimum, sample.albedo.x);
        wood_maximum = fmaxf(wood_maximum, sample.albedo.x);
    }
    if (!hit_room(ceiling_ray, 10.0f, wood, true, scene.settings) ||
        !hit_room({{0.4f, 3.0f, 0.2f}, {0.0f, 1.0f, 0.0f}}, 10.0f, wood_repeat, true, scene.settings) ||
        wood.material != Material::diffuse || wood.tiled_floor || wood.normal.y != -1.0f ||
        wood.albedo.x != wood_repeat.albedo.x || wood_maximum - wood_minimum < 0.03f ||
        !(wood.albedo.x > wood.albedo.y && wood.albedo.y > wood.albedo.z) ||
        fabsf(wood.distance - 0.2f) > 0.0001f) {
        failed |= 128;
    }
    float marble_minimum = 1.0f, marble_maximum = 0.0f;
    float variation_x = 0.0f, variation_y = 0.0f;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 20; ++x) {
            Hit sample{};
            if (!hit_room({{-2.85f + x * 0.3f, 0.1f + y * 0.25f, 0.0f}, {0.0f, 0.0f, -1.0f}},
                          10.0f, sample, true, scene.settings) ||
                sample.material != Material::diffuse || sample.tiled_floor || sample.normal.z != 1.0f ||
                !isfinite(sample.albedo.x) || !isfinite(sample.albedo.y) || !isfinite(sample.albedo.z) ||
                sample.albedo.x < 0.0f || sample.albedo.y < 0.0f || sample.albedo.z < 0.0f ||
                sample.albedo.x > 1.0f || sample.albedo.y > 1.0f || sample.albedo.z > 1.0f) {
                failed |= 1024;
            }
            marble_minimum = fminf(marble_minimum, sample.albedo.x);
            marble_maximum = fmaxf(marble_maximum, sample.albedo.x);
            variation_x = fmaxf(variation_x,
                fabsf(sample.albedo.x - marble_albedo(sample.point + float3{0.1f, 0, 0}).x));
            variation_y = fmaxf(variation_y,
                fabsf(sample.albedo.x - marble_albedo(sample.point + float3{0, 0.1f, 0}).x));
            if (fabsf(sample.albedo.x - marble_albedo(sample.point + float3{0.0001f, 0, 0}).x) > 0.02f) {
                failed |= 1024;
            }
        }
    }
    if (!hit_room(back_ray, 10.0f, marble, true, scene.settings) ||
        !hit_room({{0.0f, 2.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, 10.0f, marble_repeat, true, scene.settings) ||
        !hit_room({{0.0f, 2.0f, -3.5f}, {0.0f, 0.0f, 1.0f}}, 10.0f, marble_backside, true, scene.settings) ||
        !hit_room(back_ray, 10.0f, classic_back) ||
        marble.albedo.x != marble_repeat.albedo.x || marble.albedo.x != marble_backside.albedo.x ||
        marble_backside.front_face || marble_backside.normal.z != -1.0f ||
        marble_minimum > 0.35f || marble_maximum < 0.75f || variation_x < 0.1f || variation_y < 0.1f ||
        classic_back.albedo.x != 0.75f || classic_back.albedo.y != 0.75f) {
        failed |= 1024;
    }
    Hit mandelbrot_inside{}, mandelbrot_outside{}, julia_inside{}, julia_outside{}, classic_left{}, classic_right{};
    const auto side_ray = [](float direction, float z) {
        return Ray{{0.0f, 2.0f, z}, {direction, 0.0f, 0.0f}};
    };
    if (!hit_room(side_ray(-1.0f, 0.0f), 10.0f, mandelbrot_inside, true) ||
        !hit_room(side_ray(-1.0f, -2.4f), 10.0f, mandelbrot_outside, true) ||
        !hit_room(side_ray(1.0f, 0.0f), 10.0f, julia_inside, true) ||
        !hit_room(side_ray(1.0f, 2.4f), 10.0f, julia_outside, true) ||
        !hit_room(side_ray(-1.0f, 0.0f), 10.0f, classic_left) ||
        !hit_room(side_ray(1.0f, 0.0f), 10.0f, classic_right) ||
        mandelbrot_inside.material != Material::diffuse || julia_inside.material != Material::diffuse ||
        mandelbrot_inside.normal.x != 1.0f || julia_inside.normal.x != -1.0f ||
        mandelbrot_outside.albedo.x <= mandelbrot_inside.albedo.x ||
        julia_outside.albedo.y <= julia_inside.albedo.y ||
        mandelbrot_outside.albedo.x <= mandelbrot_outside.albedo.y ||
        julia_outside.albedo.y <= julia_outside.albedo.x ||
        classic_left.albedo.x != 0.72f || classic_left.albedo.y != 0.10f ||
        classic_right.albedo.x != 0.10f || classic_right.albedo.y != 0.65f) {
        failed |= 256;
    }
    *failure = failed;
}

void check_loaded_ply(int device, const char* path, MeshUp up) {
    const HiprtScene loaded(device, path, up);
    if (loaded.scene().mesh_triangle_count != mesh_triangles.size() + 1) {
        throw std::runtime_error("Imported PLY did not replace the cat");
    }
    auto failure = allocate_device<unsigned int>(1, "Allocating PLY intersection check");
    hipLaunchKernelGGL(check_ply_scene, dim3(1), dim3(1), 0, 0, loaded.scene(), failure.get());
    check_hip(hipGetLastError(), "Launching PLY intersection check");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost),
              "Reading PLY intersection check");
    if (result != 0) {
        throw std::runtime_error("Imported PLY HIP RT intersection check failed");
    }
}

__global__ void check_described_scene(TraceScene scene, unsigned int* failure) {
    unsigned int failed = 0;
    Hit hit{};
    if (!hit_custom_objects({{1, 3, -1}, {0, -1, 0}}, hit, scene) ||
        fabsf(hit.distance - 0.7f) > 0.001f || hit.albedo.x != 0.8f ||
        hit.material != Material::diffuse) failed |= 1;
    const auto& torus = scene.primitives[1];
    const float3 origin = vector3(torus.center) +
        rotate_x({torus.major_radius, 0.5f, 0}, torus.tilt_sine, torus.tilt_cosine);
    const float3 direction = rotate_x({0, -1, 0}, torus.tilt_sine, torus.tilt_cosine);
    if (!hit_custom_objects({origin, direction}, hit, scene) || hit.object_id != 1 ||
        hit.material != Material::metal || fabsf(hit.distance - 0.4f) > 0.003f) failed |= 2;
    if (!hit_scene({{3, 1, 4}, {0, -1, 0}}, hit, scene) ||
        fabsf(hit.distance - 1) > 0.001f || hit.albedo.z != 0.4f) failed |= 4;
    if (!hit_scene({{1, 5, -2}, {0, 1, 0}}, hit, scene) ||
        hit.material != Material::emissive || fabsf(hit.distance - 1) > 0.001f) failed |= 8;
    for (int index = 0; index < 2; ++index) {
        if (!hit_objects({{static_cast<float>(index * 2), 0.4f, 1}, {0, 0, -1}}, hit, scene) ||
            hit.object_id != static_cast<unsigned int>(index + 2) ||
            hit.material != (index == 0 ? Material::diffuse : Material::metal) ||
            fabsf(hit.distance - 1) > 0.001f) failed |= 16;
    }
    if (scene.settings->camera_position.z != 12 || scene.settings->half_view_height != 0.4f ||
        scene.settings->light.radiance.y != 4) failed |= 32;
    Hit ceiling{}, back{};
    if (scene.settings->surfaces[1].kind != SurfaceKind::wood ||
        scene.settings->surfaces[2].kind != SurfaceKind::marble) {
        failed |= 64;
    }
    if (!hit_scene({{3, 5, 4}, {0, 1, 0}}, ceiling, scene) || ceiling.material != Material::diffuse ||
        fabsf(ceiling.albedo.x - wood_albedo({3, 4, 0}).x) > 0.001f) failed |= 128;
    if (!hit_scene({{3, 3, 0}, {0, 0, -1}}, back, scene) || back.material != Material::diffuse ||
        fabsf(back.albedo.x - marble_albedo({3, 3, -5}).x) > 0.001f) failed |= 256;
    *failure = failed;
}

void check_description(int device, const char* path) {
    auto description = load_scene_description(path);
    auto failure = allocate_device<unsigned int>(1, "Allocating described scene check");
    const HiprtScene scene(device, description);
    hipLaunchKernelGGL(check_described_scene, dim3(1), dim3(1), 0, 0, scene.scene(), failure.get());
    check_hip(hipGetLastError(), "Launching described scene check");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost),
              "Reading described scene check");
    if (result != 0) {
        throw std::runtime_error("Described scene check failed (flags " + std::to_string(result) + ")");
    }
    description.primitives.clear();
    const HiprtScene mesh_only(device, description);
    if (mesh_only.scene().geometry || !mesh_only.scene().mesh) {
        throw std::runtime_error("Mesh-only scene built incorrect geometry");
    }
    description.meshes.clear();
    const HiprtScene room_only(device, description);
    if (room_only.scene().geometry || room_only.scene().mesh) {
        throw std::runtime_error("Room-only scene built incorrect geometry");
    }
    std::cout << "Scene description GPU checks passed.\n";
}

struct TextProbe {
    Ray ray;
    float minimum = 0.001f, maximum = 100;
    bool expected = false;
    TextIntersection hit{};
    unsigned int object_id = 0;
    SceneMaterial material;
};

__global__ void check_text_probes(TraceScene scene, const TextProbe* probes, unsigned int count,
                                  unsigned int* failures) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const auto& probe = probes[index];
    hiprtRay ray{};
    ray.origin = probe.ray.origin;
    ray.direction = probe.ray.direction;
    ray.minT = probe.minimum;
    ray.maxT = probe.maximum;
    hiprtGeomCustomTraversalClosest traversal(
        scene.geometry, ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const hiprtHit actual = traversal.getNextHit();
    unsigned int failed = actual.hasHit() != probe.expected ? 1 : 0;
    if (actual.hasHit() && probe.expected &&
        (fabsf(actual.t - probe.hit.distance) > 0.0001f ||
         dot(normalize(actual.normal), vector3(probe.hit.normal)) < 0.999f)) failed |= 2;
    if (probe.minimum == 0.001f && probe.maximum == 100) {
        Hit hit{};
        const bool found = hit_objects(probe.ray, hit, scene);
        const float3 outward = vector3(probe.hit.normal);
        if (found != probe.expected || (found &&
            (fabsf(hit.distance - probe.hit.distance) > 0.0001f || hit.object_id != probe.object_id ||
             hit.material != probe.material.kind || hit.albedo.x != probe.material.albedo.x ||
             hit.front_face != (dot(probe.ray.direction, outward) < 0)))) failed |= 4;
    }
    failures[index] = failed;
}

__global__ void check_mixed_text(TraceScene scene, TextProbe probe, unsigned int* failure) {
    unsigned int failed = 0;
    Hit hit{};
    if (!hit_objects(probe.ray, hit, scene) || hit.object_id != probe.object_id + 1 ||
        hit.material != probe.material.kind || fabsf(hit.distance - probe.hit.distance) > 0.0001f) failed |= 1;
    if (!hit_objects({{-20, 1, -18}, {0, 0, -1}}, hit, scene) || hit.object_id != 0 ||
        fabsf(hit.distance - 1.5f) > 0.0001f) failed |= 2;
    const float3 a = mesh_points[0], b = mesh_points[2], c = mesh_points[1];
    const float3 normal = normalize(cross(b - a, c - a));
    const float3 center = (a + b + c) / 3;
    if (!hit_objects({center + 0.25f * normal, -normal}, hit, scene) || hit.object_id != 3 ||
        hit.material != Material::phong || fabsf(hit.distance - 0.25f) > 0.0001f) failed |= 4;
    const TextCurve cubic[2]{{{0, 0, 0, 2}, {0, 3, -3, 0}}, {{2, -2, 0, 0}, {0, 0, 0, 0}}};
    TextGlyph glyph;
    glyph.depth = 0.5f;
    glyph.curve_count = 2;
    glyph.normal_sign = -1;
    TextIntersection curved_hit{};
    if (!intersect_text(glyph, cubic, {0.25f, 2, 0.25f}, {0, -1, 0}, 0.001f, 100, curved_hit) ||
        fabsf(curved_hit.distance - 1.25f) > 0.0001f) failed |= 8;
    *failure = failed;
}

void check_text_scene(int device, const char* font) {
    auto description = load_scene_description(builtin_scene_path(false));
    description.primitives.clear();
    description.meshes.clear();
    description.texts = {
        {font, "BO S", 1.5f, 0.3f, {10, 0, 0}, {{0.8f, 0.2f, 0.1f}, Material::metal, 0}},
        {font, "BO", 0.7f, 0.5f, {10, 3, -1}, {{0.2f, 0.5f, 0.9f}, Material::glass, 0}}
    };
    TextGeometry geometry;
    for (unsigned int index = 0; index < description.texts.size(); ++index) {
        append_text_geometry(geometry, description.texts[index], index);
    }
    std::vector<TextProbe> probes;
    const auto append_probe = [&](Ray ray, float minimum = 0.001f, float maximum = 100) {
        TextProbe probe;
        probe.ray = ray; probe.minimum = minimum; probe.maximum = maximum;
        for (const auto& glyph : geometry.glyphs) {
            TextIntersection hit{};
            if (intersect_text(glyph, geometry.curves.data(),
                               {ray.origin.x, ray.origin.y, ray.origin.z},
                               {ray.direction.x, ray.direction.y, ray.direction.z},
                               minimum, maximum, hit)) {
                maximum = hit.distance;
                probe.hit = hit;
                probe.expected = true;
                probe.object_id = glyph.object_id;
                probe.material = glyph.material;
            }
        }
        probes.push_back(probe);
    };
    unsigned int cap_hits = 0, cap_misses = 0;
    for (const auto& glyph : geometry.glyphs) {
        const float z = glyph.position.z;
        for (int row = 0; row < 13; ++row) {
            const float y = glyph.minimum.y + (glyph.maximum.y - glyph.minimum.y) * (row + 0.37f) / 13;
            for (int column = 0; column < 13; ++column) {
                const float x = glyph.minimum.x + (glyph.maximum.x - glyph.minimum.x) * (column + 0.41f) / 13;
                append_probe({{x, y, z + 2}, {0, 0, -1}});
                if (probes.back().expected) ++cap_hits; else ++cap_misses;
                append_probe({{x, y, z - 2}, {0, 0, 1}});
                append_probe({{x, y, z + 2}, {0, 0, -1}}, 1.9f);
                append_probe({{x, y, z + 2}, {0, 0, -1}}, 0.001f, 1);
                append_probe({{x, y, z + glyph.depth * 0.5f}, {1, 0, 0}});
                append_probe({{x, y, z + glyph.depth * 0.5f}, {0, -1, 0}});
                append_probe({{x, y, z + 2}, {0.1f, 0.03f, -1}});
            }
        }
    }
    if (!cap_hits || !cap_misses) throw std::runtime_error("Text test must exercise filled and empty cap regions");
    const HiprtScene scene(device, description);
    if (!scene.scene().geometry || scene.scene().mesh || scene.scene().mesh_triangle_count != 0) {
        throw std::runtime_error("Text must use custom geometry, not triangles");
    }
    auto device_probes = allocate_device<TextProbe>(probes.size(), "Allocating text probes");
    auto failures = allocate_device<unsigned int>(probes.size(), "Allocating text probe results");
    check_hip(hipMemcpy(device_probes.get(), probes.data(), probes.size() * sizeof(TextProbe), hipMemcpyHostToDevice),
              "Uploading text probes");
    const auto count = static_cast<unsigned int>(probes.size());
    hipLaunchKernelGGL(check_text_probes, dim3((count + 63) / 64), dim3(64), 0, 0,
                       scene.scene(), device_probes.get(), count, failures.get());
    check_hip(hipGetLastError(), "Launching text intersection checks");
    std::vector<unsigned int> results(count);
    check_hip(hipMemcpy(results.data(), failures.get(), results.size() * sizeof(unsigned int), hipMemcpyDeviceToHost),
              "Reading text intersection checks");
    for (unsigned int index = 0; index < count; ++index) {
        if (results[index]) throw std::runtime_error("Text HIP RT probe " + std::to_string(index) +
                                                    " failed (flags " + std::to_string(results[index]) + ")");
    }
    ScenePrimitive sphere;
    sphere.center = {-20, 1, -20};
    sphere.radius = 0.5f;
    description.primitives.push_back(sphere);
    SceneMesh crystal;
    crystal.kind = SceneMeshKind::crystal;
    description.meshes.push_back(crystal);
    const HiprtScene mixed(device, description);
    const auto sample = std::find_if(probes.begin(), probes.end(), [](const TextProbe& p) { return p.expected; });
    hipLaunchKernelGGL(check_mixed_text, dim3(1), dim3(1), 0, 0, mixed.scene(), *sample, failures.get());
    check_hip(hipGetLastError(), "Launching mixed text checks");
    unsigned int mixed_failure = 0;
    check_hip(hipMemcpy(&mixed_failure, failures.get(), sizeof(mixed_failure), hipMemcpyDeviceToHost),
              "Reading mixed text checks");
    if (mixed_failure) throw std::runtime_error("Mixed text scene failed (flags " + std::to_string(mixed_failure) + ")");
    description.primitives.clear();
    description.meshes.clear();
    description.texts[0].value = "   ";
    description.texts.resize(1);
    const HiprtScene spaces(device, description);
    if (spaces.scene().geometry || spaces.scene().mesh) throw std::runtime_error("Spaces must not create geometry");
    std::cout << "Text HIP RT checks passed (" << count << " rays, no triangulation).\n";
}

}

int main(int argc, char** argv) {
    try {
        const bool text_mode = argc == 3 && std::string_view(argv[1]) == "--text";
        if (!text_mode && argc != 1 && argc != 4 && argc != 5) {
            throw std::runtime_error("Usage: hiprt_tests [y-up.ply z-up.ply orientation.ply [scene.hipscene]] | --text font");
        }
        const SelectedDevice device = select_device();
        std::cout << "Testing on " << device.properties.name << " ("
                  << device.properties.gcnArchName << ")...\n";
        // Establish the HIP context before HIP RT queries it.
        const auto context_buffer = allocate_device<unsigned int>(1, "Initializing HIP context");
        if (!text_mode) run_analytic_intersection_tests();
        if (text_mode) {
            check_text_scene(device.id, argv[2]);
            return 0;
        }
        const HiprtScene scene(device.id);
        run_checks(scene.scene());
        if (argc >= 4) {
            std::vector<float4> y_vertices, z_vertices;
            append_ply_mesh(y_vertices, argv[1], MeshUp::y);
            append_ply_mesh(z_vertices, argv[2], MeshUp::z);
            if (y_vertices.size() != 3 || z_vertices.size() != y_vertices.size()) {
                throw std::runtime_error("PLY orientation produced incorrect vertex count");
            }
            if (argc == 5) check_description(device.id, argv[4]);
            for (std::size_t index = 0; index < y_vertices.size(); ++index) {
                const float4 y = y_vertices[index], z = z_vertices[index];
                if (y.x != z.x || y.y != z.y || y.z != z.z) {
                    throw std::runtime_error("Z-up PLY was not rotated and fitted like the Y-up fixture");
                }
                std::vector<float4> rotated;
                append_ply_mesh(rotated, argv[3], MeshUp::z);
                constexpr std::array<float3, 3> expected{{
                    {1.40f, 0.02f, 2.20f}, {2.30f, 0.02f, 2.20f}, {1.85f, 0.92f, 1.30f}}};
                if (rotated.size() != expected.size()) {
                    throw std::runtime_error("Z-up PLY orientation produced incorrect vertex count");
                }
                for (std::size_t index = 0; index < expected.size(); ++index) {
                    const float4 point = rotated[index];
                    if (std::abs(point.x - expected[index].x) > 0.0001f ||
                        std::abs(point.y - expected[index].y) > 0.0001f ||
                        std::abs(point.z - expected[index].z) > 0.0001f) {
                        throw std::runtime_error("Z-up PLY orientation or fitting is incorrect");
                    }
                }
            }
            check_loaded_ply(device.id, argv[1], MeshUp::y);
            check_loaded_ply(device.id, argv[2], MeshUp::z);
            std::cout << "PLY mesh HIP RT intersection checks passed.\n";
            const HiprtScene detail_scene(device.id, argv[2], MeshUp::z, true);
            if (detail_scene.scene().mesh_triangle_count != 1) {
                throw std::runtime_error("Detail scene must contain only the supplied mesh");
            }
            auto failure = allocate_device<unsigned int>(1, "Allocating detail scene check");
            hipLaunchKernelGGL(check_detail_scene, dim3(1), dim3(1), 0, 0, detail_scene.scene(), failure.get());
            check_hip(hipGetLastError(), "Launching detail scene check");
            unsigned int result = 0;
            check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost),
                      "Reading detail scene check");
            if (result != 0) {
                throw std::runtime_error("Detail scene check failed (flags " + std::to_string(result) + ")");
            }
            std::cout << "Detail scene, torus light and materials checks passed.\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HIP RT tests failed: " << error.what() << '\n';
        return 1;
    }
}
