#include "renderer.h"
#include "dielectric.h"
#include "path_scattering.h"
#include "area_light.h"
#include "rendering/mis.h"
#include "math/random.h"

#include <hiprt/hiprt_device.h>
#include <hiprt/hiprt_math.h>

#include <png.h>

#include <cstddef>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct GpuRenderer {
    hiprtScene scene;
    hiprtFuncTable functions;
    uchar3* pixels;
    int width;
    int height;
    GpuCamera camera;
    const SurfaceMaterial* materials;
    const std::uint32_t* material_ids;
    float3 light_vector;
    float3 light_intensity;
    bool point_light;
    int max_depth;
    int samples_per_pixel;
    const AreaLight* area_lights;
    unsigned int area_light_count;

    template <Integrator Mode, bool Glass, bool AreaLights>
    __device__ void render_pixel(unsigned int x, unsigned int y) const
    {
        if (x >= width || y >= height) {
            return;
        }

        float3 sum{0, 0, 0};
        for (int sample = 0; sample < samples_per_pixel; ++sample) {
            float2 offset{0.5f, 0.5f};
            if constexpr (Mode == Integrator::path || AreaLights) {
                auto random = sample_random(x, y, sample);
                if (samples_per_pixel > 1) {
                    offset = {random.next(), random.next()};
                }
                if constexpr (Mode == Integrator::path) {
                    sum = sum + trace_path<Glass, AreaLights>(camera.generate_ray(x, y, width, height, offset), random);
                } else {
                    sum = sum + trace<Glass, true>(camera.generate_ray(x, y, width, height, offset), random);
                }
            } else {
                hip_demo::Random random{1};
                if (samples_per_pixel > 1) {
                    random = sample_random(x, y, sample);
                    offset = {random.next(), random.next()};
                }
                sum = sum + trace<Glass, false>(camera.generate_ray(x, y, width, height, offset), random);
            }
        }
        const auto color = sum / static_cast<float>(samples_per_pixel);
        pixels[static_cast<std::size_t>(y) * width + x] =
            {color_byte(color.x), color_byte(color.y), color_byte(color.z)};
    }

private:
    __device__ hip_demo::Random sample_random(unsigned int x, unsigned int y, int sample) const
    {
        const std::uint32_t seed = (x + y * width + 1u) * 747796405u +
            (static_cast<std::uint32_t>(sample) + 1u) * 2891336453u;
        return {seed == 0 ? 1u : seed};
    }

    __device__ float3 surface_normal(const hiprtHit& hit) const
    {
        // Normals use the inverse transpose, not the ordinary vector transform.
        const auto inverse = hiprtGetWorldToObjectFrameMatrix(scene, hit.instanceID);
        const auto& m = inverse.matrix;
        const auto n = hiprt::normalize(hit.normal);
        return hiprt::normalize(float3{
            m[0][0] * n.x + m[1][0] * n.y + m[2][0] * n.z,
            m[0][1] * n.x + m[1][1] * n.y + m[2][1] * n.z,
            m[0][2] * n.x + m[1][2] * n.y + m[2][2] * n.z,
        });
    }

    __device__ const AreaLight* find_area_light(unsigned int instance_id) const
    {
        for (unsigned int i = 0; i < area_light_count; ++i) {
            if (area_lights[i].instance_id == instance_id) {
                return area_lights + i;
            }
        }
        return nullptr;
    }

    __device__ bool area_shadowed(float3 point, float3 normal, float3 light_point,
                                  unsigned int light_instance) const
    {
        auto ray = secondary_ray(point, normal, light_point - point);
        const auto offset = light_point - ray.origin;
        const float distance = sqrtf(hiprt::dot(offset, offset));
        if (distance == 0.0f) {
            return false;
        }
        ray.direction = offset / distance;
        ray.maxT = nextafterf(distance, 0.0f);
        hiprtSceneTraversalClosest traversal(scene, ray, hiprtFullRayMask, hiprtTraversalHintShadowRays,
                                             nullptr, functions);
        const auto hit = traversal.getNextHit();
        return hit.hasHit() && hit.instanceID != light_instance;
    }

    template <Integrator Mode>
    __device__ float3 sample_area_direct(float3 point, float3 normal, float3 incoming,
                                         const SurfaceMaterial& material, hip_demo::Random& random,
                                         float diffuse_weight, bool continuation) const
    {
        const unsigned int index = area_light_count > 1
            ? static_cast<unsigned int>(double(random.next()) * area_light_count) : 0;
        const auto& source = area_lights[index];
        const auto rectangle = world_rectangle(scene, source.instance_id);
        const auto light_point = rectangle.center + (2.0f * random.next() - 1.0f) * rectangle.half_u +
                                                   (2.0f * random.next() - 1.0f) * rectangle.half_v;
        const auto offset = light_point - point;
        if (hiprt::dot(offset, offset) == 0.0f) {
            return {0, 0, 0};
        }
        const auto direction = hiprt::normalize(offset);
        const float cosine = hiprt::dot(normal, direction);
        const double light_pdf = rectangle_light_pdf(rectangle, area_light_count, point, light_point);
        if (cosine <= 0.0f || light_pdf <= 0 ||
            area_shadowed(point, normal, light_point, source.instance_id)) {
            return {0, 0, 0};
        }
        float weight = 1;
        auto scattering = material.base_color * (cosine / hip_demo::pi);
        if constexpr (Mode == Integrator::path) {
            const float bsdf_pdf = continuation ? diffuse_weight * cosine / hip_demo::pi : 0.0f;
            weight = hip_demo::power_heuristic(light_pdf, bsdf_pdf);
        } else {
            const auto reflected = 2.0f * cosine * normal - direction;
            const float specular = hiprt::dot(normal, -incoming) > 0.0f
                ? material.specular_strength *
                    powf(fmaxf(0.0f, hiprt::dot(reflected, -incoming)), material.shininess) : 0.0f;
            scattering = scattering + float3{specular, specular, specular} / hip_demo::pi;
        }
        return scattering * source.radiance * static_cast<float>(weight / light_pdf);
    }

    template <bool Glass, bool AreaLights>
    __device__ float3 trace_path(hiprtRay ray, hip_demo::Random& random) const
    {
        float3 radiance{0, 0, 0};
        float3 throughput{1, 1, 1};
        float previous_pdf = 0;
        float3 previous_point{};
        for (int depth = 0; ; ++depth) {
            hiprtSceneTraversalClosest traversal(scene, ray, hiprtFullRayMask, hiprtTraversalHintDefault,
                                                 nullptr, functions);
            const auto hit = traversal.getNextHit();
            if (!hit.hasHit()) {
                radiance = radiance + throughput * float3{12.0f / 255, 16.0f / 255, 24.0f / 255};
                break;
            }
            const auto outward_normal = surface_normal(hit);
            const auto normal = hiprt::dot(ray.direction, outward_normal) < 0.0f
                ? outward_normal : -outward_normal;
            const auto point = ray.origin + hit.t * ray.direction;
            const auto material = materials[material_ids[hit.instanceID]];
            if constexpr (AreaLights) {
                if (const auto* source = find_area_light(hit.instanceID)) {
                    if (hiprt::dot(outward_normal, -ray.direction) > 0.0f) {
                        const float weight = previous_pdf > 0
                            ? hip_demo::power_heuristic(previous_pdf,
                                rectangle_light_pdf(world_rectangle(scene, hit.instanceID),
                                                    area_light_count, previous_point, point))
                            : 1.0f;
                        radiance = radiance + throughput * source->radiance * weight;
                    }
                    break;
                }
            }
            const float diffuse_weight = (1.0f - material.reflectivity) * (1.0f - material.transmission);
            if (diffuse_weight > 0.0f) {
                if constexpr (AreaLights) {
                    radiance = radiance + throughput * diffuse_weight *
                        sample_area_direct<Integrator::path>(point, normal, ray.direction, material,
                                                           random, diffuse_weight, depth < max_depth);
                }
                const auto to_light = point_light ? light_vector - point : light_vector;
                const float distance_squared = hiprt::dot(to_light, to_light);
                if (distance_squared > 0.0f) {
                    const auto direction_to_light = hiprt::normalize(to_light);
                    const float cosine = hiprt::dot(normal, direction_to_light);
                    if (cosine > 0.0f && !shadowed(point, normal, direction_to_light)) {
                        const float attenuation = point_light ? 1.0f / distance_squared : 1.0f;
                        radiance = radiance + throughput * diffuse_weight * material.base_color *
                            (cosine / hip_demo::pi) * light_intensity * attenuation;
                    }
                }
            }
            if (depth == max_depth) {
                break;
            }
            const auto scatter = sample_path_scatter<Glass>(material, ray.direction, outward_normal, random);
            if constexpr (AreaLights) {
                previous_pdf = scatter.pdf;
                previous_point = point;
            }
            throughput = throughput * scatter.weight;
            if (throughput.x == 0.0f && throughput.y == 0.0f && throughput.z == 0.0f) {
                break;
            }
            ray = secondary_ray(point, outward_normal, scatter.direction);
        }
        return radiance;
    }

    struct PendingRay {
        hiprtRay ray;
        float3 throughput;
        int depth;
    };

    template <bool Glass, bool AreaLights>
    __device__ float3 trace(hiprtRay ray, hip_demo::Random& random) const
    {
        float3 radiance{0, 0, 0};
        float3 throughput{1, 1, 1};
        PendingRay pending[Glass ? maximum_ray_depth : 1];
        int pending_count = 0;
        int depth = 0;
        for (;;) {
            hiprtSceneTraversalClosest traversal(scene, ray, hiprtFullRayMask, hiprtTraversalHintDefault,
                                                 nullptr, functions);
            const hiprtHit hit = traversal.getNextHit();
            bool emissive_hit = false;
            if constexpr (AreaLights) {
                if (hit.hasHit()) {
                    if (const auto* source = find_area_light(hit.instanceID)) {
                        if (hiprt::dot(surface_normal(hit), -ray.direction) > 0.0f) {
                            radiance = radiance + throughput * source->radiance;
                        }
                        emissive_hit = true;
                    }
                }
            }
            if (!hit.hasHit()) {
                radiance = radiance + throughput * float3{12.0f / 255, 16.0f / 255, 24.0f / 255};
            } else if (!emissive_hit) {
                const auto normal = surface_normal(hit);
                const auto point = ray.origin + hit.t * ray.direction;
                const auto to_light = point_light ? light_vector - point : light_vector;
                const float distance_squared = hiprt::dot(to_light, to_light);
                // An ideal point source is singular at its position; leave that point unlit.
                const auto direction_to_light = distance_squared > 0.0f
                    ? hiprt::normalize(to_light) : float3{0.0f, 0.0f, 0.0f};
                const float attenuation = point_light && distance_squared > 0.0f
                    ? 1.0f / distance_squared : 1.0f;
                const auto material = materials[material_ids[hit.instanceID]];
                float local_weight = 1.0f - material.reflectivity;
                float reflection_weight = material.reflectivity;
                float transmission_weight = 0.0f;
                DielectricSplit split;
                if constexpr (Glass) {
                    if (material.transmission > 0.0f && local_weight > 0.0f) {
                        split = split_dielectric(ray.direction, normal, material.ior);
                        const float glass_weight = local_weight * material.transmission;
                        reflection_weight += glass_weight * split.fresnel;
                        transmission_weight = glass_weight * (1.0f - split.fresnel);
                        local_weight *= 1.0f - material.transmission;
                    }
                }
                const float cosine = hiprt::dot(normal, direction_to_light);
                float3 lit{0.0f, 0.0f, 0.0f};
                const auto view = -ray.direction;
                if (local_weight > 0.0f && cosine > 0.0f &&
                    !shadowed(point, normal, direction_to_light)) {
                    const auto reflected = 2.0f * cosine * normal - direction_to_light;
                    const float specular = hiprt::dot(normal, view) > 0.0f
                        ? material.specular_strength *
                            powf(fmaxf(0.0f, hiprt::dot(reflected, view)), material.shininess)
                        : 0.0f;
                    lit = (material.base_color * cosine + float3{specular, specular, specular}) *
                          light_intensity * attenuation;
                }
                if constexpr (AreaLights) {
                    if (local_weight > 0.0f) {
                        lit = lit + sample_area_direct<Integrator::ray>(
                            point, normal, ray.direction, material, random, 0, false);
                    }
                }
                radiance = radiance + throughput * local_weight * lit;
                if (depth < max_depth) {
                    const auto reflected = hiprt::normalize(
                        ray.direction - 2.0f * hiprt::dot(ray.direction, normal) * normal);
                    if constexpr (Glass) {
                        if (transmission_weight > 0.0f) {
                            if (reflection_weight > 0.0f) {
                                // Depth-first traversal defers at most one sibling per depth.
                                pending[pending_count++] = {
                                    secondary_ray(point, normal, reflected),
                                    throughput * reflection_weight, depth + 1};
                            }
                            ray = secondary_ray(point, normal, split.transmitted);
                            throughput = throughput * transmission_weight;
                            ++depth;
                            continue;
                        }
                    }
                    if (reflection_weight > 0.0f) {
                        ray = secondary_ray(point, normal, reflected);
                        throughput = throughput * reflection_weight;
                        ++depth;
                        continue;
                    }
                }
            }
            if constexpr (Glass) {
                if (pending_count > 0) {
                    const auto next = pending[--pending_count];
                    ray = next.ray;
                    throughput = next.throughput;
                    depth = next.depth;
                    continue;
                }
            }
            break;
        }
        return radiance;
    }

    __device__ static hiprtRay secondary_ray(float3 point, float3 normal, float3 direction)
    {
        hiprtRay ray{};
        ray.origin = point + normal * (hiprt::dot(direction, normal) > 0.0f ? 0.001f : -0.001f);
        ray.direction = direction;
        ray.minT = 0.0f;
        ray.maxT = std::numeric_limits<float>::max();
        return ray;
    }

    __device__ bool shadowed(float3 point, float3 normal, float3 direction_to_light) const
    {
        // A small world-space bias avoids intersecting the surface we just hit.
        hiprtRay shadow_ray{};
        shadow_ray.origin = point + 0.001f * normal;
        shadow_ray.direction = direction_to_light;
        shadow_ray.minT = 0.0f;
        if (point_light) {
            // Recompute from the biased origin so the segment ends at the light.
            const auto to_light = light_vector - shadow_ray.origin;
            const float distance = sqrtf(hiprt::dot(to_light, to_light));
            if (distance == 0.0f) {
                return false;
            }
            shadow_ray.direction = to_light / distance;
            shadow_ray.maxT = nextafterf(distance, 0.0f);
        }
        hiprtSceneTraversalAnyHit traversal(
            scene, shadow_ray, hiprtFullRayMask, hiprtTraversalHintShadowRays, nullptr, functions);
        return traversal.getNextHit().hasHit();
    }

    __device__ static unsigned char color_byte(float value)
    {
        return static_cast<unsigned char>(255.0f * fminf(1.0f, fmaxf(0.0f, value)));
    }
};

template <Integrator Mode, bool Glass, bool AreaLights>
__global__ void render_kernel(GpuRenderer renderer)
{
    renderer.render_pixel<Mode, Glass, AreaLights>(blockIdx.x * blockDim.x + threadIdx.x,
                                                  blockIdx.y * blockDim.y + threadIdx.y);
}

std::size_t pixel_count(int width, int height)
{
    static_assert(sizeof(uchar3) == 3, "PNG input requires tightly packed RGB bytes.");
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Image dimensions must be positive.");
    }
    if (static_cast<std::size_t>(width) >
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / height / sizeof(uchar3)) {
        throw std::overflow_error("Image dimensions are too large.");
    }
    return static_cast<std::size_t>(width) * height;
}

}

DirectionalLight::DirectionalLight(float3 direction_to_light)
{
    const float length_squared = hiprt::dot(direction_to_light, direction_to_light);
    if (!std::isfinite(length_squared) || length_squared <= 0.0f) {
        throw std::invalid_argument("Light direction must have a finite, nonzero squared length.");
    }
    direction_ = hiprt::normalize(direction_to_light);
}

Renderer::Renderer(int width, int height, int max_depth, int samples_per_pixel, Integrator integrator)
    : width_(width), height_(height), max_depth_(max_depth),
      samples_per_pixel_(samples_per_pixel), integrator_(integrator),
      pixels_(pixel_count(width, height)), device_pixels_(pixels_.size())
{
    if (max_depth < 0 || max_depth > maximum_ray_depth) {
        throw std::invalid_argument("Maximum ray depth must be between 0 and " +
                                    std::to_string(maximum_ray_depth) + ".");
    }
    if (samples_per_pixel <= 0) {
        throw std::invalid_argument("Samples per pixel must be positive.");
    }
    if (integrator != Integrator::ray && integrator != Integrator::path) {
        throw std::invalid_argument("Integrator must be ray or path.");
    }
}

PointLight::PointLight(float3 position, float3 color, float intensity)
    : position_(position), color_(color), intensity_(intensity)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        throw std::invalid_argument("Point light position must be finite.");
    }
    if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
        color.x < 0.0f || color.x > 1.0f || color.y < 0.0f || color.y > 1.0f ||
        color.z < 0.0f || color.z > 1.0f) {
        throw std::invalid_argument("Point light color must be finite and in [0, 1].");
    }
    if (!std::isfinite(intensity) || intensity < 0.0f) {
        throw std::invalid_argument("Point light intensity must be finite and nonnegative.");
    }
}

RenderTimings Renderer::render(const Scene& scene, const Camera& camera, const DirectionalLight& light)
{
    return render_impl(scene, camera, light.direction(), {1.0f, 1.0f, 1.0f}, false);
}

RenderTimings Renderer::render(const Scene& scene, const Camera& camera, const PointLight& light)
{
    return render_impl(scene, camera, light.position(), light.color() * light.intensity(), true);
}

RenderTimings Renderer::render_impl(const Scene& scene, const Camera& camera, float3 light_vector,
                           float3 light_intensity, bool point_light)
{
    rendered_ = false;
    const auto begin = std::chrono::steady_clock::now();
    const GpuRenderer renderer{scene.get(), scene.functions(), device_pixels_.get(), width_, height_, camera.get(),
                               scene.materials(), scene.material_ids(), light_vector, light_intensity,
                               point_light, max_depth_, samples_per_pixel_,
                               scene.area_lights(), scene.area_light_count()};

    const dim3 block(16, 16);
    const dim3 grid((width_ - 1) / block.x + 1, (height_ - 1) / block.y + 1);
    check_hip(hipEventRecord(start_.get(), nullptr), "Recording GPU render start");
    if (integrator_ == Integrator::path) {
        if (scene.area_light_count() != 0) {
            if (scene.has_transmission()) {
                hipLaunchKernelGGL((render_kernel<Integrator::path, true, true>), grid, block, 0, 0, renderer);
            } else {
                hipLaunchKernelGGL((render_kernel<Integrator::path, false, true>), grid, block, 0, 0, renderer);
            }
        } else if (scene.has_transmission()) {
            hipLaunchKernelGGL((render_kernel<Integrator::path, true, false>), grid, block, 0, 0, renderer);
        } else {
            hipLaunchKernelGGL((render_kernel<Integrator::path, false, false>), grid, block, 0, 0, renderer);
        }
    } else {
        if (scene.area_light_count() != 0) {
            if (scene.has_transmission()) {
                hipLaunchKernelGGL((render_kernel<Integrator::ray, true, true>), grid, block, 0, 0, renderer);
            } else {
                hipLaunchKernelGGL((render_kernel<Integrator::ray, false, true>), grid, block, 0, 0, renderer);
            }
        } else if (scene.has_transmission()) {
            hipLaunchKernelGGL((render_kernel<Integrator::ray, true, false>), grid, block, 0, 0, renderer);
        } else {
            hipLaunchKernelGGL((render_kernel<Integrator::ray, false, false>), grid, block, 0, 0, renderer);
        }
    }
    check_hip(hipGetLastError(), "Launching image kernel");
    check_hip(hipEventRecord(stop_.get(), nullptr), "Recording GPU render end");
    check_hip(hipEventSynchronize(stop_.get()), "Waiting for image kernel");
    float gpu_ms = 0.0f;
    check_hip(hipEventElapsedTime(&gpu_ms, start_.get(), stop_.get()), "Measuring GPU render time");

    const auto download_begin = std::chrono::steady_clock::now();
    device_pixels_.download(pixels_);
    const auto end = std::chrono::steady_clock::now();
    rendered_ = true;
    return {gpu_ms,
            std::chrono::duration<float, std::milli>(end - download_begin).count(),
            std::chrono::duration<float, std::milli>(end - begin).count()};
}

void Renderer::save(const std::filesystem::path& output) const
{
    if (!rendered_) {
        throw std::logic_error("Render an image before saving it.");
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width_;
    image.height = height_;
    image.format = PNG_FORMAT_RGB;

    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels_.data(), 0, nullptr)) {
        throw std::runtime_error("Cannot encode image: " + output.string() + ": " + image.message);
    }
    if (size > static_cast<png_alloc_size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::overflow_error("Encoded PNG is too large.");
    }
    std::vector<unsigned char> encoded(size);
    if (!png_image_write_to_memory(&image, encoded.data(), &size, 0, pixels_.data(), 0, nullptr)) {
        throw std::runtime_error("Cannot encode image: " + output.string() + ": " + image.message);
    }

    std::ofstream file(output, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open image: " + output.string());
    }
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(size));
    file.close();
    if (!file) {
        throw std::runtime_error("Cannot write image: " + output.string());
    }
}
