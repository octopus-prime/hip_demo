#pragma once

#include "camera.h"
#include "gpu_event.h"
#include "scene.h"

#include <filesystem>
#include <vector>

class DirectionalLight {
public:
    explicit DirectionalLight(float3 direction_to_light);
    float3 direction() const noexcept { return direction_; }

private:
    float3 direction_;
};

class PointLight {
public:
    explicit PointLight(float3 position, float3 color = {1.0f, 1.0f, 1.0f}, float intensity = 1.0f);
    float3 position() const noexcept { return position_; }
    float3 color() const noexcept { return color_; }
    float intensity() const noexcept { return intensity_; }

private:
    float3 position_;
    float3 color_;
    float intensity_;
};

struct RenderTimings {
    float gpu_ms;
    float download_ms;
    float total_ms;
};

inline constexpr int maximum_ray_depth = 8;

enum class Integrator { ray, path };

class Renderer {
public:
    Renderer(int width, int height, int max_depth = 3, int samples_per_pixel = 1,
             Integrator integrator = Integrator::ray);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    RenderTimings render(const Scene& scene, const Camera& camera,
                const DirectionalLight& light = DirectionalLight({0.0f, 0.0f, 1.0f}));
    RenderTimings render(const Scene& scene, const Camera& camera, const PointLight& light);
    void save(const std::filesystem::path& output) const;

private:
    RenderTimings render_impl(const Scene& scene, const Camera& camera, float3 light_vector,
                     float3 light_intensity, bool point_light);

    int width_;
    int height_;
    int max_depth_;
    int samples_per_pixel_;
    Integrator integrator_;
    std::vector<uchar3> pixels_;
    DeviceBuffer<uchar3> device_pixels_;
    GpuEvent start_;
    GpuEvent stop_;
    bool rendered_ = false;
};
