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
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int image_width = 640;
constexpr int image_height = 480;
constexpr int samples_per_pixel = 1024;
constexpr int samples_per_batch = 128;
constexpr int maximum_bounces = 8;
constexpr float sun_cos_angle = 0.9992f;
constexpr float3 sphere_center{-0.9f, 0.86f, 0.0f};
constexpr float sphere_radius = 0.86f;
constexpr float3 torus_center{0.95f, 1.0f, 0.0f};
constexpr float torus_major_radius = 0.58f;
constexpr float torus_minor_radius = 0.24f;

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
};

struct Hit {
    float distance;
    float3 point;
    float3 normal;
    float3 albedo;
    Material material;
    float roughness;
    bool front_face;
};

__device__ void set_face_normal(Hit& hit, const Ray& ray, float3 outward_normal) {
    hit.front_face = dot(ray.direction, outward_normal) < 0.0f;
    hit.normal = hit.front_face ? outward_normal : -outward_normal;
}

__device__ void set_sphere_material(Hit& hit) {
    hit.albedo = {0.92f, 0.58f, 0.22f};
    hit.material = Material::metal;
    hit.roughness = 0.12f;
}

__device__ void set_torus_material(Hit& hit) {
    hit.albedo = {0.88f, 0.97f, 1.0f};
    hit.material = Material::glass;
    hit.roughness = 0.0f;
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
    set_sphere_material(hit);
    return true;
}

__device__ float torus_distance(float3 point, float3 center, float major_radius, float minor_radius) {
    const float3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ bool hit_torus(const Ray& ray, float maximum_distance, Hit& hit) {
    constexpr float3 center = torus_center;
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
    set_torus_material(hit);
    return true;
}

__device__ bool hit_floor(const Ray& ray, float maximum_distance, Hit& hit) {
    if (fabsf(ray.direction.y) < 1.0e-6f) {
        return false;
    }
    const float distance = -ray.origin.y / ray.direction.y;
    if (distance < 0.001f || distance >= maximum_distance) {
        return false;
    }

    hit.distance = distance;
    hit.point = ray.origin + distance * ray.direction;
    set_face_normal(hit, ray, {0.0f, 1.0f, 0.0f});
    hit.albedo = {0.62f, 0.66f, 0.72f};
    hit.material = Material::diffuse;
    hit.roughness = 1.0f;
    return true;
}

// Each backend defines TraceScene before including this header and implements object traversal.
__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene);

__device__ bool hit_scene(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_objects(ray, closest_hit, scene);
    const float closest_distance = found ? closest_hit.distance : 1.0e30f;
    Hit candidate{};
    if (hit_floor(ray, closest_distance, candidate)) {
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ float3 sun_direction() {
    return normalize({-0.45f, 0.82f, 0.35f});
}

__device__ float3 sun_radiance() {
    return {5.0f, 4.5f, 3.8f};
}

__device__ float3 sample_sun(Random& random) {
    const float3 direction = sun_direction();
    const float3 tangent = normalize(cross({0.0f, 1.0f, 0.0f}, direction));
    const float3 bitangent = cross(direction, tangent);
    const float cosine = 1.0f - random.next() * (1.0f - sun_cos_angle);
    const float sine = sqrtf(fmaxf(0.0f, 1.0f - cosine * cosine));
    const float angle = 6.28318530718f * random.next();
    return cosine * direction + sine * (cosf(angle) * tangent + sinf(angle) * bitangent);
}

__device__ float3 sky_color(float3 direction, bool include_sun) {
    const float blend = 0.5f * (direction.y + 1.0f);
    float3 color = (1.0f - blend) * float3{0.88f, 0.91f, 0.96f} +
        blend * float3{0.34f, 0.56f, 0.9f};
    if (include_sun && dot(direction, sun_direction()) > sun_cos_angle) {
        color = color + sun_radiance();
    }
    return color;
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
    bool include_sun = true;

    for (int bounce = 0; bounce < maximum_bounces; ++bounce) {
        Hit hit{};
        if (!hit_scene(ray, hit, scene)) {
            radiance = radiance + throughput * sky_color(ray.direction, include_sun);
            break;
        }

        if (hit.material == Material::metal) {
            include_sun = true;
            const float3 reflected = reflect(ray.direction, hit.normal);
            ray.direction = normalize(reflected + hit.roughness * random_in_unit_sphere(random));
            if (dot(ray.direction, hit.normal) <= 0.0f) {
                break;
            }
            throughput = throughput * hit.albedo;
        } else if (hit.material == Material::glass) {
            include_sun = true;
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
            const float3 light_direction = sample_sun(random);
            const float cosine = fmaxf(0.0f, dot(hit.normal, light_direction));
            Hit shadow_hit{};
            if (cosine > 0.0f &&
                !hit_scene({hit.point + hit.normal * 0.002f, light_direction}, shadow_hit, scene)) {
                // Solid angle of the sun divided by pi for Lambertian reflection.
                radiance = radiance + throughput * hit.albedo * sun_radiance() *
                    (2.0f * (1.0f - sun_cos_angle) * cosine);
            }
            // Direct sun is already accounted for at this diffuse surface.
            include_sun = false;
            ray.direction = normalize(hit.normal + random_unit_vector(random));
            throughput = throughput * hit.albedo;
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

    const float3 camera_position{0.0f, 1.9f, 5.8f};
    const float3 target{0.0f, 0.9f, 0.0f};
    const float3 forward = normalize(target - camera_position);
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

template <typename SceneOwner>
void render_image(const std::string& output_path, int sample_count) {
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
    std::cout << "Rendering on " << device_properties.name << " ("
              << device_properties.gcnArchName << ")...\n";

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

    SceneOwner scene_owner(selected_device);
    const TraceScene scene = scene_owner.scene();

    const dim3 block_size{16, 16};
    const dim3 grid_size{
        static_cast<unsigned int>((image_width + block_size.x - 1) / block_size.x),
        static_cast<unsigned int>((image_height + block_size.y - 1) / block_size.y),
    };
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
    check_hip(hipDeviceSynchronize(), "Waiting for the HIP renderer");

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
        std::cerr << "HIP demo failed: " << error.what() << '\n';
        return 1;
    }
}
