#pragma once

#include <hip/hip_runtime.h>

#include "hip_demo_architectures.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int image_width = 3840;
constexpr int image_height = 2160;
constexpr int samples_per_pixel = 1024;
// Keep individual 4K kernels short enough for GPU watchdogs.
constexpr int samples_per_batch = 8;
constexpr int maximum_bounces = 8;
constexpr float pi = 3.14159265359f;
constexpr unsigned int scene_object_count = 7;
constexpr float sphere_radius = 0.48f;
constexpr float torus_major_radius = 0.43f;
constexpr float torus_minor_radius = 0.14f;
constexpr float3 camera_position{0.0f, 2.0f, 8.5f};
constexpr float3 camera_target{0.0f, 1.8f, 0.0f};
constexpr float room_half_width = 3.0f;
constexpr float room_height = 4.0f;
constexpr float room_half_depth = 2.5f;
constexpr float light_half_width = 0.9f;
constexpr float light_half_depth = 0.7f;
constexpr float light_center_z = -0.4f;
constexpr float phong_exponent = 40.0f;
constexpr float phong_specular_reflectance = 0.9f;

struct SceneObject {
    float3 center;
    bool sphere;
    float tilt_sine;
    float tilt_cosine;
};

__host__ __device__ SceneObject scene_object(unsigned int index) {
    const bool sphere = index % 2 == 0;
    const unsigned int tilt = index / 2;
    constexpr float sin60 = 0.86602540378f;
    const float sine = sphere ? 0.0f : (tilt == 0 ? -sin60 : tilt == 1 ? -0.5f : 0.5f);
    const float cosine = sphere ? 1.0f : (tilt == 0 ? 0.5f : sin60);
    const float absolute_sine = sine < 0.0f ? -sine : sine;
    float x = 0.0f;
    float z = -0.9f;
    switch (index) {
    case 0: x = -1.65f; break;
    case 1: x = -0.50f; break;
    case 2: x = 0.75f; break;
    case 3: x = 1.90f; break;
    case 4: x = -1.05f; z = 0.85f; break;
    case 5: x = 0.75f; z = 0.85f; break;
    case 6: x = -2.05f; z = 0.85f; break;
    }
    return {{x, sphere ? sphere_radius : torus_minor_radius + torus_major_radius * absolute_sine, z},
            sphere, sine, cosine};
}

__host__ __device__ float3 rotate_x(float3 value, float sine, float cosine) {
    return {value.x, cosine * value.y - sine * value.z, sine * value.y + cosine * value.z};
}

__host__ __device__ constexpr float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ float3 cross(float3 a, float3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

__device__ float3 normalize(float3 value) {
    return value / sqrtf(dot(value, value));
}

__device__ float clamp01(float value) {
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ float3 reflect(float3 direction, float3 normal) {
    return direction - normal * (2.0f * dot(direction, normal));
}

__device__ bool refract(float3 direction, float3 normal, float eta, float3& result) {
    const float cos_theta = fminf(dot(-direction, normal), 1.0f);
    const float3 perpendicular = eta * (direction + cos_theta * normal);
    const float parallel_squared = 1.0f - dot(perpendicular, perpendicular);
    if (parallel_squared < 0.0f) {
        return false;
    }
    result = perpendicular - sqrtf(parallel_squared) * normal;
    return true;
}

struct Random {
    std::uint32_t state;

    __device__ float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

__device__ float3 random_in_unit_sphere(Random& random) {
    while (true) {
        const float3 point{
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
        };
        if (dot(point, point) < 1.0f) {
            return point;
        }
    }
}

__device__ float3 random_unit_vector(Random& random) {
    return normalize(random_in_unit_sphere(random));
}

struct Ray {
    float3 origin;
    float3 direction;
};

enum class Material : std::uint8_t {
    diffuse,
    metal,
    glass,
    phong,
    emissive,
};

struct Hit {
    float distance;
    float3 point;
    float3 normal;
    float3 albedo;
    Material material;
    float roughness;
    bool front_face;
    unsigned int object_id;
};

__device__ void set_face_normal(Hit& hit, const Ray& ray, float3 outward_normal) {
    hit.front_face = dot(ray.direction, outward_normal) < 0.0f;
    hit.normal = hit.front_face ? outward_normal : -outward_normal;
}

__device__ void set_object_material(Hit& hit, unsigned int object_id) {
    hit.roughness = 0.0f;
    switch (object_id) {
    case 0:
        hit.albedo = {0.92f, 0.58f, 0.22f};
        hit.material = Material::metal;
        hit.roughness = 0.08f;
        break;
    case 1:
    case 6:
        hit.albedo = {0.96f, 0.98f, 1.0f};
        hit.material = Material::glass;
        break;
    case 2:
        hit.albedo = {0.12f, 0.28f, 0.82f};
        hit.material = Material::phong;
        break;
    case 3:
        hit.albedo = {0.85f, 0.48f, 0.08f};
        hit.material = Material::phong;
        break;
    case 4:
        hit.albedo = {0.96f, 0.96f, 0.96f};
        hit.material = Material::metal;
        break;
    default:
        hit.albedo = {0.55f, 0.15f, 0.55f};
        hit.material = Material::diffuse;
        break;
    }
}

__device__ bool hit_sphere(const Ray& ray, float3 center, float radius, float maximum_distance, Hit& hit) {
    const float3 offset = ray.origin - center;
    const float half_b = dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = half_b * half_b - c;
    if (discriminant < 0.0f) {
        return false;
    }

    const float root = sqrtf(discriminant);
    float distance = -half_b - root;
    if (distance < 0.001f || distance >= maximum_distance) {
        distance = -half_b + root;
    }
    if (distance < 0.001f || distance >= maximum_distance) {
        return false;
    }

    hit.distance = distance;
    hit.point = ray.origin + distance * ray.direction;
    set_face_normal(hit, ray, (hit.point - center) / radius);
    return true;
}

__device__ float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ bool hit_torus(const Ray& ray, float3 center, float maximum_distance, Hit& hit) {
    constexpr float major_radius = torus_major_radius;
    constexpr float minor_radius = torus_minor_radius;
    constexpr float bound_radius = major_radius + minor_radius;

    const float3 bound_offset = ray.origin - center;
    const float half_b = dot(bound_offset, ray.direction);
    const float discriminant = half_b * half_b -
        (dot(bound_offset, bound_offset) - bound_radius * bound_radius);
    if (discriminant < 0.0f) {
        return false;
    }

    const float root = sqrtf(discriminant);
    const float start = fmaxf(0.001f, -half_b - root);
    const float end = fminf(maximum_distance, -half_b + root);
    if (start >= end) {
        return false;
    }

    float distance = start;
    bool found = false;
    for (int step = 0; step < 128 && distance < end; ++step) {
        const float3 point = ray.origin + distance * ray.direction;
        const float signed_distance = torus_distance(point, center, major_radius, minor_radius);
        if (fabsf(signed_distance) < 0.001f) {
            found = true;
            break;
        }
        distance += fmaxf(fabsf(signed_distance) * 0.75f, 0.0005f);
    }
    if (!found || distance >= maximum_distance) {
        return false;
    }

    const float3 point = ray.origin + distance * ray.direction;
    constexpr float epsilon = 0.001f;
    const float3 normal = normalize({
        torus_distance(point + float3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - float3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + float3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - float3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + float3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius) -
            torus_distance(point - float3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius),
    });

    hit.distance = distance;
    hit.point = point;
    set_face_normal(hit, ray, normal);
    return true;
}

__device__ bool hit_primitive(const Ray& ray, unsigned int object_id, float maximum_distance, Hit& hit) {
    const SceneObject object = scene_object(object_id);
    bool found;
    if (object.sphere) {
        found = hit_sphere(ray, object.center, sphere_radius, maximum_distance, hit);
    } else {
        const Ray local_ray{
            rotate_x(ray.origin - object.center, -object.tilt_sine, object.tilt_cosine),
            rotate_x(ray.direction, -object.tilt_sine, object.tilt_cosine),
        };
        found = hit_torus(local_ray, {0.0f, 0.0f, 0.0f}, maximum_distance, hit);
        if (found) {
            hit.point = ray.origin + hit.distance * ray.direction;
            hit.normal = rotate_x(hit.normal, object.tilt_sine, object.tilt_cosine);
        }
    }
    if (found) {
        hit.object_id = object_id;
        set_object_material(hit, object_id);
    }
    return found;
}

__device__ bool hit_room(const Ray& ray, float maximum_distance, Hit& hit) {
    bool found = false;
    for (int wall = 0; wall < 5; ++wall) {
        float origin, direction, position;
        float3 normal;
        if (wall < 2) {
            origin = ray.origin.y;
            direction = ray.direction.y;
            position = wall == 0 ? 0.0f : room_height;
            normal = wall == 0 ? float3{0.0f, 1.0f, 0.0f} : float3{0.0f, -1.0f, 0.0f};
        } else if (wall == 2) {
            origin = ray.origin.z;
            direction = ray.direction.z;
            position = -room_half_depth;
            normal = {0.0f, 0.0f, 1.0f};
        } else {
            origin = ray.origin.x;
            direction = ray.direction.x;
            position = wall == 3 ? -room_half_width : room_half_width;
            normal = wall == 3 ? float3{1.0f, 0.0f, 0.0f} : float3{-1.0f, 0.0f, 0.0f};
        }
        if (fabsf(direction) < 1.0e-6f) {
            continue;
        }
        const float distance = (position - origin) / direction;
        if (distance < 0.001f || distance >= maximum_distance) {
            continue;
        }
        const float3 point = ray.origin + distance * ray.direction;
        if (fabsf(point.x) > room_half_width + 0.0001f ||
            point.y < -0.0001f || point.y > room_height + 0.0001f ||
            fabsf(point.z) > room_half_depth + 0.0001f) {
            continue;
        }
        maximum_distance = distance;
        hit.distance = distance;
        hit.point = point;
        set_face_normal(hit, ray, normal);
        hit.albedo = wall == 3 ? float3{0.72f, 0.10f, 0.10f} :
            wall == 4 ? float3{0.10f, 0.65f, 0.16f} : float3{0.75f, 0.75f, 0.75f};
        hit.material = wall == 1 && fabsf(point.x) <= light_half_width &&
            fabsf(point.z - light_center_z) <= light_half_depth ? Material::emissive : Material::diffuse;
        hit.roughness = 1.0f;
        found = true;
    }
    return found;
}

// The HIP RT renderer defines TraceScene and implements object traversal.
__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene);

__device__ bool hit_scene(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_objects(ray, closest_hit, scene);
    const float closest_distance = found ? closest_hit.distance : 1.0e30f;
    Hit candidate{};
    if (hit_room(ray, closest_distance, candidate)) {
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ float3 light_radiance() {
    return {12.0f, 11.5f, 10.5f};
}

__device__ float3 sample_phong_lobe(float3 axis, Random& random) {
    const float3 reference = fabsf(axis.y) < 0.99f ? float3{0.0f, 1.0f, 0.0f} : float3{1.0f, 0.0f, 0.0f};
    const float3 tangent = normalize(cross(reference, axis));
    const float3 bitangent = cross(axis, tangent);
    const float cosine = powf(random.next(), 1.0f / (phong_exponent + 1.0f));
    const float sine = sqrtf(fmaxf(0.0f, 1.0f - cosine * cosine));
    const float angle = 2.0f * pi * random.next();
    return cosine * axis + sine * (cosf(angle) * tangent + sinf(angle) * bitangent);
}

__device__ float reflectance(float cosine, float refraction_index) {
    float base_reflectance = (1.0f - refraction_index) / (1.0f + refraction_index);
    base_reflectance *= base_reflectance;
    return base_reflectance + (1.0f - base_reflectance) *
        powf(1.0f - cosine, 5.0f);
}

__device__ float3 trace(Ray ray, Random& random, TraceScene scene) {
    float3 radiance{0.0f, 0.0f, 0.0f};
    float3 throughput{1.0f, 1.0f, 1.0f};
    bool include_emission = true;

    for (int bounce = 0; bounce < maximum_bounces; ++bounce) {
        Hit hit{};
        if (!hit_scene(ray, hit, scene)) {
            break;
        }

        if (hit.material == Material::emissive) {
            if (include_emission && hit.front_face) {
                radiance = radiance + throughput * light_radiance();
            }
            break;
        } else if (hit.material == Material::metal) {
            include_emission = true;
            const float3 reflected = reflect(ray.direction, hit.normal);
            ray.direction = normalize(reflected + hit.roughness * random_in_unit_sphere(random));
            if (dot(ray.direction, hit.normal) <= 0.0f) {
                break;
            }
            throughput = throughput * hit.albedo;
        } else if (hit.material == Material::glass) {
            include_emission = true;
            constexpr float refraction_index = 1.5f;
            const float eta = hit.front_face ? 1.0f / refraction_index : refraction_index;
            const float cosine = fminf(dot(-ray.direction, hit.normal), 1.0f);
            const float sine = sqrtf(1.0f - cosine * cosine);
            const bool total_internal_reflection = eta * sine > 1.0f;
            const bool should_reflect = total_internal_reflection ||
                reflectance(cosine, refraction_index) > random.next();
            float3 refracted{};
            if (should_reflect || !refract(ray.direction, hit.normal, eta, refracted)) {
                ray.direction = reflect(ray.direction, hit.normal);
            } else {
                ray.direction = refracted;
                throughput = throughput * hit.albedo;
            }
        } else {
            const float3 light_point{(2.0f * random.next() - 1.0f) * light_half_width,
                                     room_height,
                                     light_center_z + (2.0f * random.next() - 1.0f) * light_half_depth};
            const float3 shadow_origin = hit.point + hit.normal * 0.002f;
            const float3 to_light = light_point - shadow_origin;
            const float distance_squared = dot(to_light, to_light);
            const float distance = sqrtf(distance_squared);
            const float3 light_direction = to_light / distance;
            const float cosine = fmaxf(0.0f, dot(hit.normal, light_direction));
            const float light_cosine = fmaxf(0.0f, light_direction.y);
            Hit shadow_hit{};
            if (cosine > 0.0f && light_cosine > 0.0f &&
                (!hit_scene({shadow_origin, light_direction}, shadow_hit, scene) ||
                 shadow_hit.distance >= distance - 0.001f)) {
                float3 scattering = hit.albedo * (cosine / pi);
                if (hit.material == Material::phong) {
                    const float3 reflected = normalize(reflect(ray.direction, hit.normal));
                    // This normalized lobe integrates to one even before the surface cosine.
                    const float lobe = powf(fmaxf(0.0f, dot(reflected, light_direction)), phong_exponent) *
                        ((phong_exponent + 1.0f) / (2.0f * pi));
                    scattering = 0.5f * (scattering + float3{1.0f, 1.0f, 1.0f} *
                        (phong_specular_reflectance * cosine * lobe));
                }
                const float light_area = 4.0f * light_half_width * light_half_depth;
                radiance = radiance + throughput * scattering * light_radiance() *
                    (light_cosine * light_area / distance_squared);
            }
            include_emission = false;
            if (hit.material == Material::phong && random.next() < 0.5f) {
                const float3 reflected = normalize(reflect(ray.direction, hit.normal));
                ray.direction = sample_phong_lobe(reflected, random);
                const float outgoing_cosine = fmaxf(0.0f, dot(ray.direction, hit.normal));
                if (outgoing_cosine == 0.0f) {
                    break;
                }
                // The normalized Phong lobe is its own sampling PDF.
                throughput = throughput * (phong_specular_reflectance * outgoing_cosine);
            } else {
                ray.direction = normalize(hit.normal + random_unit_vector(random));
                throughput = throughput * hit.albedo;
            }
        }

        ray.origin = hit.point + hit.normal * (dot(ray.direction, hit.normal) > 0.0f ? 0.002f : -0.002f);
    }
    return radiance;
}

__device__ float tone_map(float value) {
    value = fmaxf(0.0f, value);
    return sqrtf(value / (1.0f + value));
}

__global__ void render_kernel(std::uint32_t* pixels, float3* sums, TraceScene scene, int width, int height,
                              int first_sample, int sample_end, int sample_count) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const float3 forward = normalize(camera_target - camera_position);
    const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const float3 up = cross(right, forward);
    constexpr float half_view_height = 0.41421356f;
    constexpr float aspect_ratio = static_cast<float>(image_width) / image_height;
    const int pixel_index = x + y * width;
    float3 accumulated = first_sample == 0 ? float3{0.0f, 0.0f, 0.0f} : sums[pixel_index];

    for (int sample = first_sample; sample < sample_end; ++sample) {
        const std::uint32_t seed = static_cast<std::uint32_t>(
            (x + y * width + 1) * 747796405u + (sample + 1) * 2891336453u);
        Random random{seed == 0 ? 1u : seed};
        const float u = (2.0f * (static_cast<float>(x) + random.next()) / width - 1.0f) *
            aspect_ratio * half_view_height;
        const float v = (1.0f - 2.0f * (static_cast<float>(y) + random.next()) / height) *
            half_view_height;
        const float3 direction = normalize(forward + u * right + v * up);
        accumulated = accumulated + trace({camera_position, direction}, random, scene);
    }

    if (sample_end == sample_count) {
        const float3 color = accumulated / static_cast<float>(sample_count);
        const auto red = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.x)));
        const auto green = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.y)));
        const auto blue = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.z)));
        pixels[pixel_index] = red | (green << 8) | (blue << 16);
    } else {
        sums[pixel_index] = accumulated;
    }
}

struct HipBufferDeleter {
    template <typename T>
    void operator()(T* buffer) const noexcept {
        if (buffer != nullptr) {
            const hipError_t result = hipFree(buffer);
            if (result != hipSuccess) {
                std::cerr << "Freeing GPU buffer: " << hipGetErrorString(result) << '\n';
            }
        }
    }
};

void check_hip(hipError_t result, std::string_view operation) {
    if (result != hipSuccess) {
        throw std::runtime_error(
            std::string(operation) + ": " + hipGetErrorString(result));
    }
}

struct HipEvent {
    hipEvent_t value = nullptr;

    HipEvent() {
        check_hip(hipEventCreate(&value), "Creating GPU timing event");
    }

    ~HipEvent() {
        if (value) {
            const hipError_t result = hipEventDestroy(value);
            if (result != hipSuccess) {
                std::cerr << "Destroying GPU timing event: " << hipGetErrorString(result) << '\n';
            }
        }
    }
};

bool matches_architecture(std::string_view target, std::string_view device) {
    const auto target_separator = target.find(':');
    if (target.substr(0, target_separator) != device.substr(0, device.find(':'))) {
        return false;
    }
    if (target_separator == std::string_view::npos) {
        return true;
    }
    target.remove_prefix(target_separator + 1);
    while (!target.empty()) {
        const auto separator = target.find(':');
        const auto feature = target.substr(0, separator);
        std::string_view device_features = device;
        bool found = false;
        while (device_features.find(':') != std::string_view::npos) {
            device_features.remove_prefix(device_features.find(':') + 1);
            if (device_features.substr(0, device_features.find(':')) == feature) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        target.remove_prefix(separator + 1);
    }
    return true;
}

struct SelectedDevice {
    int id;
    hipDeviceProp_t properties;
};

SelectedDevice select_device() {
    int device_count = 0;
    check_hip(hipGetDeviceCount(&device_count), "Detecting HIP devices");
    if (device_count == 0) {
        throw std::runtime_error("No HIP-compatible GPU was found.");
    }
    int selected_device = -1;
    hipDeviceProp_t device_properties{};
    for (int device = 0; device < device_count; ++device) {
        hipDeviceProp_t candidate{};
        check_hip(hipGetDeviceProperties(&candidate, device), "Reading HIP device properties");
        std::string_view compiled_architectures(HIP_DEMO_ARCHITECTURES);
        bool supported = false;
        while (!compiled_architectures.empty()) {
            const auto separator = compiled_architectures.find(';');
            const auto target = compiled_architectures.substr(0, separator);
            if (matches_architecture(target, candidate.gcnArchName)) {
                supported = true;
                break;
            }
            if (separator == std::string_view::npos) {
                break;
            }
            compiled_architectures.remove_prefix(separator + 1);
        }
        if (supported && (selected_device < 0 || (device_properties.integrated && !candidate.integrated))) {
            selected_device = device;
            device_properties = candidate;
        }
    }
    if (selected_device < 0) {
        throw std::runtime_error(
            "No HIP device matches the compiled GPU architectures (" HIP_DEMO_ARCHITECTURES
            "). Reconfigure with HIP_ARCHITECTURE for an available GPU.");
    }
    check_hip(hipSetDevice(selected_device), "Selecting HIP device");
    return {selected_device, device_properties};
}

template <typename SceneOwner>
void render_image(const std::string& output_path, int sample_count) {
    const SelectedDevice device = select_device();
    std::cout << "Rendering on " << device.properties.name << " ("
              << device.properties.gcnArchName << ")...\n";
    std::cout << "Scene: " << (scene_object_count + 1) / 2 << " spheres, "
              << scene_object_count / 2 << " tori; " << sample_count << " samples per pixel.\n";

    const std::size_t pixel_count = static_cast<std::size_t>(image_width) * image_height;
    std::unique_ptr<std::uint32_t, HipBufferDeleter> device_pixels;
    std::uint32_t* allocation = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&allocation), pixel_count * sizeof(std::uint32_t)),
              "Allocating the image on the GPU");
    device_pixels.reset(allocation);
    std::unique_ptr<float3, HipBufferDeleter> device_sums;
    float3* sums_allocation = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&sums_allocation), pixel_count * sizeof(float3)),
              "Allocating the sample sums on the GPU");
    device_sums.reset(sums_allocation);

    SceneOwner scene_owner(device.id);
    const TraceScene scene = scene_owner.scene();

    const dim3 block_size{16, 16};
    const dim3 grid_size{
        static_cast<unsigned int>((image_width + block_size.x - 1) / block_size.x),
        static_cast<unsigned int>((image_height + block_size.y - 1) / block_size.y),
    };
    HipEvent start;
    HipEvent stop;
    check_hip(hipEventRecord(start.value, nullptr), "Starting GPU render timer");
    for (int first_sample = 0; first_sample < sample_count;) {
        const int sample_end = first_sample + std::min(samples_per_batch, sample_count - first_sample);
        hipLaunchKernelGGL(
            render_kernel,
            grid_size,
            block_size,
            0,
            0,
            device_pixels.get(),
            device_sums.get(),
            scene,
            image_width,
            image_height,
            first_sample,
            sample_end,
            sample_count);
        check_hip(hipGetLastError(), "Launching the HIP renderer");
        first_sample = sample_end;
    }
    check_hip(hipEventRecord(stop.value, nullptr), "Stopping GPU render timer");
    check_hip(hipEventSynchronize(stop.value), "Waiting for the HIP renderer");
    float render_milliseconds = 0.0f;
    check_hip(hipEventElapsedTime(&render_milliseconds, start.value, stop.value), "Reading GPU render time");
    std::cout << "GPU render time: " << std::fixed << std::setprecision(3)
              << render_milliseconds / 1000.0f << " s\n";

    std::vector<std::uint32_t> pixels(pixel_count);
    check_hip(
        hipMemcpy(pixels.data(), device_pixels.get(), pixel_count * sizeof(std::uint32_t),
                  hipMemcpyDeviceToHost),
        "Copying the rendered image to the CPU");

    std::ofstream image(output_path, std::ios::binary);
    if (!image) {
        throw std::runtime_error("Could not open output image: " + output_path);
    }
    constexpr std::uint32_t row_size = (image_width * 3 + 3) & ~3u;
    constexpr std::uint32_t header_size = 54;
    std::array<unsigned char, header_size> header{};
    const auto write_header_value = [&header](std::size_t offset, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            header[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    header[0] = 'B';
    header[1] = 'M';
    write_header_value(2, header_size + row_size * image_height, 4);
    write_header_value(10, header_size, 4);
    write_header_value(14, 40, 4);
    write_header_value(18, image_width, 4);
    write_header_value(22, static_cast<std::uint32_t>(-image_height), 4);
    write_header_value(26, 1, 2);
    write_header_value(28, 24, 2);
    write_header_value(34, row_size * image_height, 4);
    image.write(reinterpret_cast<const char*>(header.data()), header.size());
    constexpr std::array<char, 3> padding{};
    for (int y = 0; y < image_height; ++y) {
        for (int x = 0; x < image_width; ++x) {
            const std::uint32_t pixel = pixels[x + y * image_width];
            const std::array<unsigned char, 3> color{
                static_cast<unsigned char>((pixel >> 16) & 0xff),
                static_cast<unsigned char>((pixel >> 8) & 0xff),
                static_cast<unsigned char>(pixel & 0xff),
            };
            image.write(reinterpret_cast<const char*>(color.data()), color.size());
        }
        image.write(padding.data(), row_size - image_width * 3);
    }
    image.close();
    if (!image) {
        throw std::runtime_error("Failed while writing output image: " + output_path);
    }
    std::cout << "Saved " << output_path << " (" << image_width << 'x' << image_height
              << ", " << sample_count << " samples per pixel).\n";
}

}

template <typename SceneOwner>
int run_demo(int argc, char** argv, const char* default_output_path) {
    try {
        const std::string output_path = argc > 1 ? argv[1] : default_output_path;
        if (argc > 3) {
            throw std::runtime_error("Usage: renderer [output.bmp] [samples-per-pixel]");
        }
        int sample_count = samples_per_pixel;
        if (argc > 2) {
            const std::string_view input(argv[2]);
            const auto result = std::from_chars(input.data(), input.data() + input.size(), sample_count);
            if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || sample_count <= 0) {
                throw std::runtime_error("Samples per pixel must be a positive integer.");
            }
        }
        render_image<SceneOwner>(output_path, sample_count);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HIP RT demo failed: " << error.what() << '\n';
        return 1;
    }
}
