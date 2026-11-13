#include <hip/hip_runtime.h>

#include <array>
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
constexpr int samples_per_pixel = 24;
constexpr int maximum_bounces = 8;

struct Vec3 {
    float x;
    float y;
    float z;
};

__host__ __device__ constexpr Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

__host__ __device__ constexpr Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

__host__ __device__ constexpr Vec3 operator-(Vec3 value) {
    return {-value.x, -value.y, -value.z};
}

__host__ __device__ constexpr Vec3 operator*(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

__host__ __device__ constexpr Vec3 operator*(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

__host__ __device__ constexpr Vec3 operator*(float scale, Vec3 value) {
    return value * scale;
}

__host__ __device__ constexpr Vec3 operator/(Vec3 value, float scale) {
    return value * (1.0f / scale);
}

__host__ __device__ constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

__device__ Vec3 normalize(Vec3 value) {
    return value / sqrtf(dot(value, value));
}

__device__ float clamp01(float value) {
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ Vec3 reflect(Vec3 direction, Vec3 normal) {
    return direction - normal * (2.0f * dot(direction, normal));
}

__device__ bool refract(Vec3 direction, Vec3 normal, float eta, Vec3& result) {
    const float cos_theta = fminf(dot(-direction, normal), 1.0f);
    const Vec3 perpendicular = eta * (direction + cos_theta * normal);
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

__device__ Vec3 random_in_unit_sphere(Random& random) {
    while (true) {
        const Vec3 point{
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
            2.0f * random.next() - 1.0f,
        };
        if (dot(point, point) < 1.0f) {
            return point;
        }
    }
}

__device__ Vec3 random_unit_vector(Random& random) {
    return normalize(random_in_unit_sphere(random));
}

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

enum class Material : std::uint8_t {
    diffuse,
    metal,
    glass,
};

struct Hit {
    float distance;
    Vec3 point;
    Vec3 normal;
    Vec3 albedo;
    Material material;
    float roughness;
    bool front_face;
};

__device__ void set_face_normal(Hit& hit, const Ray& ray, Vec3 outward_normal) {
    hit.front_face = dot(ray.direction, outward_normal) < 0.0f;
    hit.normal = hit.front_face ? outward_normal : -outward_normal;
}

__device__ bool hit_sphere(const Ray& ray, Vec3 center, float radius, float maximum_distance, Hit& hit) {
    const Vec3 offset = ray.origin - center;
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
    hit.albedo = {0.92f, 0.58f, 0.22f};
    hit.material = Material::metal;
    hit.roughness = 0.12f;
    return true;
}

__device__ float torus_distance(Vec3 point, Vec3 center, float major_radius, float minor_radius) {
    const Vec3 local = point - center;
    const float ring_distance = sqrtf(local.x * local.x + local.z * local.z) - major_radius;
    return sqrtf(ring_distance * ring_distance + local.y * local.y) - minor_radius;
}

__device__ bool hit_torus(const Ray& ray, float maximum_distance, Hit& hit) {
    constexpr Vec3 center{0.95f, 1.0f, 0.0f};
    constexpr float major_radius = 0.58f;
    constexpr float minor_radius = 0.24f;
    constexpr float bound_radius = major_radius + minor_radius;

    const Vec3 bound_offset = ray.origin - center;
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
        const Vec3 point = ray.origin + distance * ray.direction;
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

    const Vec3 point = ray.origin + distance * ray.direction;
    constexpr float epsilon = 0.001f;
    const Vec3 normal = normalize({
        torus_distance(point + Vec3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - Vec3{epsilon, 0.0f, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + Vec3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius) -
            torus_distance(point - Vec3{0.0f, epsilon, 0.0f}, center, major_radius, minor_radius),
        torus_distance(point + Vec3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius) -
            torus_distance(point - Vec3{0.0f, 0.0f, epsilon}, center, major_radius, minor_radius),
    });

    hit.distance = distance;
    hit.point = point;
    set_face_normal(hit, ray, normal);
    hit.albedo = {0.88f, 0.97f, 1.0f};
    hit.material = Material::glass;
    hit.roughness = 0.0f;
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

__device__ bool hit_scene(const Ray& ray, Hit& closest_hit) {
    bool found = false;
    float closest_distance = 1.0e30f;
    Hit candidate{};

    if (hit_sphere(ray, {-0.9f, 0.86f, 0.0f}, 0.86f, closest_distance, candidate)) {
        closest_hit = candidate;
        closest_distance = candidate.distance;
        found = true;
    }
    if (hit_torus(ray, closest_distance, candidate)) {
        closest_hit = candidate;
        closest_distance = candidate.distance;
        found = true;
    }
    if (hit_floor(ray, closest_distance, candidate)) {
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ Vec3 sky_color(Vec3 direction) {
    const float blend = 0.5f * (direction.y + 1.0f);
    Vec3 color = (1.0f - blend) * Vec3{0.88f, 0.91f, 0.96f} +
        blend * Vec3{0.34f, 0.56f, 0.9f};
    const Vec3 sun_direction = normalize({-0.45f, 0.82f, 0.35f});
    const float sun = dot(direction, sun_direction);
    if (sun > 0.9992f) {
        color = color + Vec3{5.0f, 4.5f, 3.8f};
    }
    return color;
}

__device__ float reflectance(float cosine, float refraction_index) {
    float base_reflectance = (1.0f - refraction_index) / (1.0f + refraction_index);
    base_reflectance *= base_reflectance;
    return base_reflectance + (1.0f - base_reflectance) *
        powf(1.0f - cosine, 5.0f);
}

__device__ Vec3 trace(Ray ray, Random& random) {
    Vec3 radiance{0.0f, 0.0f, 0.0f};
    Vec3 throughput{1.0f, 1.0f, 1.0f};

    for (int bounce = 0; bounce < maximum_bounces; ++bounce) {
        Hit hit{};
        if (!hit_scene(ray, hit)) {
            radiance = radiance + throughput * sky_color(ray.direction);
            break;
        }

        if (hit.material == Material::metal) {
            const Vec3 reflected = reflect(ray.direction, hit.normal);
            ray.direction = normalize(reflected + hit.roughness * random_in_unit_sphere(random));
            if (dot(ray.direction, hit.normal) <= 0.0f) {
                break;
            }
            throughput = throughput * hit.albedo;
        } else if (hit.material == Material::glass) {
            constexpr float refraction_index = 1.5f;
            const float eta = hit.front_face ? 1.0f / refraction_index : refraction_index;
            const float cosine = fminf(dot(-ray.direction, hit.normal), 1.0f);
            const float sine = sqrtf(1.0f - cosine * cosine);
            const bool total_internal_reflection = eta * sine > 1.0f;
            const bool should_reflect = total_internal_reflection ||
                reflectance(cosine, refraction_index) > random.next();
            Vec3 refracted{};
            if (should_reflect || !refract(ray.direction, hit.normal, eta, refracted)) {
                ray.direction = reflect(ray.direction, hit.normal);
            } else {
                ray.direction = refracted;
                throughput = throughput * hit.albedo;
            }
        } else {
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

__global__ void render_kernel(std::uint32_t* pixels, int width, int height, int sample_count) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const Vec3 camera_position{0.0f, 1.9f, 5.8f};
    const Vec3 target{0.0f, 0.9f, 0.0f};
    const Vec3 forward = normalize(target - camera_position);
    const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vec3 up = cross(right, forward);
    constexpr float half_view_height = 0.41421356f;
    constexpr float aspect_ratio = static_cast<float>(image_width) / image_height;
    Vec3 accumulated{0.0f, 0.0f, 0.0f};

    for (int sample = 0; sample < sample_count; ++sample) {
        const std::uint32_t seed = static_cast<std::uint32_t>(
            (x + y * width + 1) * 747796405u + (sample + 1) * 2891336453u);
        Random random{seed == 0 ? 1u : seed};
        const float u = (2.0f * (static_cast<float>(x) + random.next()) / width - 1.0f) *
            aspect_ratio * half_view_height;
        const float v = (1.0f - 2.0f * (static_cast<float>(y) + random.next()) / height) *
            half_view_height;
        const Vec3 direction = normalize(forward + u * right + v * up);
        accumulated = accumulated + trace({camera_position, direction}, random);
    }

    const Vec3 color = accumulated / static_cast<float>(sample_count);
    const auto red = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.x)));
    const auto green = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.y)));
    const auto blue = static_cast<std::uint32_t>(255.999f * clamp01(tone_map(color.z)));
    pixels[x + y * width] = red | (green << 8) | (blue << 16);
}

struct HipBufferDeleter {
    void operator()(std::uint32_t* buffer) const noexcept {
        if (buffer != nullptr) {
            hipFree(buffer);
        }
    }
};

void check_hip(hipError_t result, std::string_view operation) {
    if (result != hipSuccess) {
        throw std::runtime_error(
            std::string(operation) + ": " + hipGetErrorString(result));
    }
}

void render_image(const std::string& output_path) {
    int device_count = 0;
    check_hip(hipGetDeviceCount(&device_count), "Detecting HIP devices");
    if (device_count == 0) {
        throw std::runtime_error("No HIP-compatible GPU was found.");
    }
    check_hip(hipSetDevice(0), "Selecting HIP device");

    hipDeviceProp_t device_properties{};
    check_hip(hipGetDeviceProperties(&device_properties, 0), "Reading HIP device properties");
    std::cout << "Rendering on " << device_properties.name << "...\n";

    const std::size_t pixel_count = static_cast<std::size_t>(image_width) * image_height;
    std::unique_ptr<std::uint32_t, HipBufferDeleter> device_pixels;
    std::uint32_t* allocation = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&allocation), pixel_count * sizeof(std::uint32_t)),
              "Allocating the image on the GPU");
    device_pixels.reset(allocation);

    const dim3 block_size{16, 16};
    const dim3 grid_size{
        static_cast<unsigned int>((image_width + block_size.x - 1) / block_size.x),
        static_cast<unsigned int>((image_height + block_size.y - 1) / block_size.y),
    };
    hipLaunchKernelGGL(
        render_kernel,
        grid_size,
        block_size,
        0,
        0,
        device_pixels.get(),
        image_width,
        image_height,
        samples_per_pixel);
    check_hip(hipGetLastError(), "Launching the HIP renderer");
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
    image << "P6\n" << image_width << ' ' << image_height << "\n255\n";
    for (const std::uint32_t pixel : pixels) {
        const std::array<unsigned char, 3> color{
            static_cast<unsigned char>(pixel & 0xff),
            static_cast<unsigned char>((pixel >> 8) & 0xff),
            static_cast<unsigned char>((pixel >> 16) & 0xff),
        };
        image.write(reinterpret_cast<const char*>(color.data()), color.size());
    }
    if (!image) {
        throw std::runtime_error("Failed while writing output image: " + output_path);
    }
    std::cout << "Saved " << output_path << " (" << image_width << 'x' << image_height
              << ", " << samples_per_pixel << " samples per pixel).\n";
}

}

int main(int argc, char** argv) {
    try {
        const std::string output_path = argc > 1 ? argv[1] : "hip_demo.ppm";
        render_image(output_path);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HIP demo failed: " << error.what() << '\n';
        return 1;
    }
}
