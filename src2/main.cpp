#include "camera.h"
#include "context.h"
#include "geometry.h"
#include "mesh.h"
#include "renderer.h"
#include "scene.h"
#include "hiprt_tutorial_assets.h"

#include <hiprt/hiprt_math.h>

#include <array>
#include <chrono>
#include <charconv>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

float elapsed_ms(Clock::time_point begin)
{
    return std::chrono::duration<float, std::milli>(Clock::now() - begin).count();
}

std::size_t free_gpu_memory()
{
    std::size_t available = 0;
    std::size_t total = 0;
    check_hip(hipMemGetInfo(&available, &total), "Querying GPU memory");
    std::cout << "GPU memory available: " << static_cast<float>(available) / (1024 * 1024)
              << " MiB / " << static_cast<float>(total) / (1024 * 1024) << " MiB\n";
    return available;
}

void render_scene(const Scene& scene, const Camera& camera, const PointLight& light, const char* output,
                  int samples_per_pixel, Integrator integrator)
{
    constexpr int width = 3840;
    constexpr int height = 2160;
    Renderer renderer(width, height, 3, samples_per_pixel, integrator);
    auto output_path = std::filesystem::path(output);
    if (integrator == Integrator::path) {
        output_path.replace_filename(output_path.stem().string() + "-path" + output_path.extension().string());
    }
    std::cout << "Rendering " << width << " x " << height << ", 1 frame.\n"
              << "Integrator: " << (integrator == Integrator::ray ? "ray" : "path") << ".\n"
              << "Maximum secondary-ray depth: 3.\n"
              << "Area lights: " << scene.area_light_count() << ".\n"
              << "Sampling: " << samples_per_pixel << " samples per pixel.\n"
              << std::flush;
    const auto timing = renderer.render(scene, camera, light);
    std::cout << "GPU render (HIP events): " << timing.gpu_ms << " ms\n"
              << "Image download (wall): " << timing.download_ms << " ms\n"
              << "Render total (wall, launch + sync + download): " << timing.total_ms << " ms\n"
              << "No BVH rebuild, PNG encoding or disk writes are included in these timings.\n";
    renderer.save(output_path);
    std::cout << "Rendered " << width << " x " << height << " pixels to " << output_path.string() << ".\n";
}

void render_dragon(const Context& context, const std::string& mesh_path, int samples_per_pixel,
                   Integrator integrator)
{
    const auto geometry_begin = Clock::now();
    Geometry dragon(context, load_ply_vertices(mesh_path));
    dragon.build();
    Geometry floor(context, UnitPlane{});
    floor.build();
    Geometry glass(context, UnitSphere{});
    glass.build();
    std::cout << "Geometry setup + BVH build (wall): " << elapsed_ms(geometry_begin) << " ms\n";
    const std::array instances{
        Instance{.geometry = dragon, .rotation_axis = {0, 1, 0}, .rotation_degrees = -20,
                 .material = {.base_color = {0.15f, 0.75f, 0.9f},
                              .specular_strength = 0.45f, .shininess = 48}},
        Instance{.geometry = floor, .translation = {0, -1.02f, 0}, .scale = {4, 1, 4},
                 .material = {.base_color = {0.18f, 0.2f, 0.24f},
                              .specular_strength = 0.15f, .shininess = 96, .reflectivity = 0.8f}},
        Instance{.geometry = glass, .translation = {0, -0.47f, 1.6f},
                 .scale = {0.55f, 0.55f, 0.55f},
                 .material = {.base_color = {1, 1, 1}, .transmission = 1, .ior = 1.5f}},
    };
    const auto scene_begin = Clock::now();
    Scene scene(context, instances);
    scene.build();
    std::cout << "Scene setup + BVH build (wall): " << elapsed_ms(scene_begin) << " ms\n"
              << "Scene BVH built: 3 instances (1 PLY mesh + 1 mirror floor + 1 glass sphere), 3 geometry BVHs.\n";
    free_gpu_memory();
    const Camera camera({0, 0.4f, 5.4f}, {0, -0.9f, 0}, {0, 1, 0}, 46);
    const PointLight light({-3, 4, 5}, {1, 0.95f, 0.85f}, 60);
    render_scene(scene, camera, light, "dragon.png", samples_per_pixel, integrator);
}

void render_cornell(const Context& context, int samples_per_pixel, Integrator integrator)
{
    Geometry plane(context, UnitPlane{});
    plane.build();
    Geometry sphere(context, UnitSphere{});
    sphere.build();
    const SurfaceMaterial white{.base_color = {0.73f, 0.73f, 0.73f}};
    const std::array instances{
        Instance{.geometry = plane, .scale = {2, 1, 2}, .material = white},
        Instance{.geometry = plane, .translation = {0, 4, 0},
                 .rotation_degrees = 180, .scale = {2, 1, 2}, .material = white},
        Instance{.geometry = plane, .translation = {0, 2, -2}, .rotation_axis = {1, 0, 0},
                 .rotation_degrees = 90, .scale = {2, 1, 2}, .material = white},
        Instance{.geometry = plane, .translation = {-2, 2, 0},
                 .rotation_degrees = -90, .scale = {2, 1, 2},
                 .material = {.base_color = {0.75f, 0.08f, 0.08f}}},
        Instance{.geometry = plane, .translation = {2, 2, 0},
                 .rotation_degrees = 90, .scale = {2, 1, 2},
                 .material = {.base_color = {0.08f, 0.75f, 0.08f}}},
        Instance{.geometry = sphere, .translation = {-0.8f, 0.7f, -0.5f},
                 .scale = {0.7f, 0.7f, 0.7f}, .material = white},
        Instance{.geometry = sphere, .translation = {0.85f, 0.7f, 0.5f},
                 .scale = {0.7f, 0.7f, 0.7f}, .material = {.transmission = 1}},
        Instance{.geometry = plane, .translation = {0, 3.98f, 0}, .rotation_degrees = 180,
                 .scale = {0.8f, 1, 0.65f}, .material = {.base_color = {0, 0, 0}},
                 .emission = {8, 8, 8}},
    };
    Scene scene(context, instances);
    scene.build();
    std::cout << "Scene BVH built: 8 instances (5 walls + 1 matte sphere + 1 glass sphere + 1 area light), 2 shared geometry BVHs.\n";
    const Camera camera({0, 2, 7}, {0, 2, 0}, {0, 1, 0}, 40);
    const PointLight light({0, 3.8f, 0.3f}, {1, 1, 1}, 0);
    render_scene(scene, camera, light, "cornell.png", samples_per_pixel, integrator);
}

}

int main(int argc, char** argv)
{
    try {
        bool small = false;
        bool dragon_scene = false;
        bool cornell = false;
        Integrator integrator = Integrator::ray;
        int samples_per_pixel = 1;
        std::string mesh_path = tutorial_ply_path;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--small") {
                small = true;
            } else if (argument == "--dragon") {
                dragon_scene = true;
            } else if (argument == "--cornell") {
                cornell = true;
            } else if (argument == "--integrator") {
                if (i + 1 == argc) {
                    throw std::invalid_argument("--integrator requires ray or path.");
                }
                const std::string_view value(argv[++i]);
                if (value == "ray") {
                    integrator = Integrator::ray;
                } else if (value == "path") {
                    integrator = Integrator::path;
                } else {
                    throw std::invalid_argument("--integrator requires ray or path.");
                }
            } else if (argument == "--samples") {
                if (i + 1 == argc) {
                    throw std::invalid_argument("--samples requires a positive integer.");
                }
                const std::string_view value(argv[++i]);
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), samples_per_pixel);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || samples_per_pixel <= 0) {
                    throw std::invalid_argument("--samples requires a positive integer.");
                }
            } else if (argument == "--ply" && i + 1 < argc) {
                mesh_path = argv[++i];
            } else {
                throw std::invalid_argument("Usage: hiprt_tutorial [--small | --dragon | --cornell] [--ply FILE] [--samples N] [--integrator ray|path] (default: 6000000 instances, 1 sample, ray)");
            }
        }
        if ((small && dragon_scene) || (cornell && (small || dragon_scene))) {
            throw std::invalid_argument("Choose only one of --small, --dragon or --cornell.");
        }
        std::cout << std::fixed << std::setprecision(3);
        Context context;
        if (cornell) {
            render_cornell(context, samples_per_pixel, integrator);
            return 0;
        }
        if (dragon_scene) {
            render_dragon(context, mesh_path, samples_per_pixel, integrator);
            return 0;
        }
        const std::array<float3, 24> vertices{{
            {0, 1, 0}, {0, 0, 1}, {1, 0, 0},
            {0, 1, 0}, {1, 0, 0}, {0, 0, -1},
            {0, 1, 0}, {0, 0, -1}, {-1, 0, 0},
            {0, 1, 0}, {-1, 0, 0}, {0, 0, 1},
            {0, -1, 0}, {1, 0, 0}, {0, 0, 1},
            {0, -1, 0}, {0, 0, -1}, {1, 0, 0},
            {0, -1, 0}, {-1, 0, 0}, {0, 0, -1},
            {0, -1, 0}, {0, 0, 1}, {-1, 0, 0},
        }};

        const auto geometry_begin = Clock::now();
        Geometry diamond(context, vertices);
        diamond.build();
        std::cout << "Diamond mesh built: 24 vertices, 8 triangles, 1 geometry BVH.\n";
        Geometry sphere(context, UnitSphere{});
        sphere.build();
        std::cout << "Analytic sphere built: 1 AABB, no triangles.\n";
        Geometry box(context, UnitBox{});
        box.build();
        std::cout << "Box mesh built: 36 vertices, 12 triangles, 1 geometry BVH.\n";
        Geometry torus(context, Torus{.major_radius = 0.8f, .minor_radius = 0.25f});
        torus.build();
        std::cout << "Analytic torus built: major radius 0.8, minor radius 0.25, 1 AABB.\n";
        Geometry dragon(context, load_ply_vertices(mesh_path));
        dragon.build();
        std::cout << "PLY mesh built: 1 shared geometry BVH.\n";
        std::cout << "Geometry setup + BVH builds (wall): " << elapsed_ms(geometry_begin) << " ms\n";

        const std::array materials{
            SurfaceMaterial{.base_color = {0.9f, 0.2f, 0.15f}, .specular_strength = 0.7f, .shininess = 16},
            SurfaceMaterial{.reflectivity = 1},
            SurfaceMaterial{.transmission = 1, .ior = 1.5f},
            SurfaceMaterial{.base_color = {0.95f, 0.45f, 0.15f}, .specular_strength = 0.8f, .shininess = 64},
            SurfaceMaterial{.base_color = {0.95f, 0.65f, 0.15f}, .specular_strength = 0.5f, .shininess = 16},
            SurfaceMaterial{.base_color = {0.15f, 0.75f, 0.9f}, .specular_strength = 0.7f, .shininess = 32},
            SurfaceMaterial{.base_color = {0.75f, 0.25f, 0.9f}, .specular_strength = 0.8f, .shininess = 32},
            SurfaceMaterial{.base_color = {0.6f, 0.85f, 0.25f}, .specular_strength = 0.6f, .shininess = 32},
        };
        const std::array prototypes{
            Instance{.geometry = sphere, .translation = {-3.9f, 1.25f, 0.0f}},
            Instance{.geometry = sphere, .translation = {-1.3f, 1.25f, 0.0f}},
            Instance{.geometry = sphere, .translation = {1.3f, 1.25f, 0.0f}},
            Instance{.geometry = torus, .translation = {3.9f, 1.25f, 0.0f},
                     .rotation_axis = {1, 0, 0}, .rotation_degrees = 65.0f},
            Instance{.geometry = diamond, .translation = {-3.9f, -1.25f, 0.0f},
                     .rotation_axis = {0.0f, 1.0f, 0.0f}, .rotation_degrees = 20.0f},
            Instance{.geometry = dragon, .translation = {-1.3f, -1.25f, 0.0f},
                     .rotation_axis = {0.0f, 1.0f, 0.0f}, .rotation_degrees = -20.0f},
            Instance{.geometry = dragon, .translation = {1.3f, -1.25f, 0.0f},
                     .rotation_axis = {0.0f, 1.0f, 0.0f}, .rotation_degrees = -45.0f,
                     .scale = {1.1f, 0.8f, 1.0f}},
            Instance{.geometry = box, .translation = {3.9f, -1.25f, 0.0f},
                     .rotation_axis = {0.5f, 1, 0}, .rotation_degrees = 30,
                     .scale = {0.75f, 0.75f, 0.75f}},
        };
        const int side = small ? 1 : 100;
        const int depth = small ? 1 : 75;
        const std::size_t count = static_cast<std::size_t>(side) * side * depth * prototypes.size();
        std::cout << "Generating " << count << " instances: " << side << " x " << side << " x "
                  << depth << " groups of eight.\n" << std::flush;
        const auto generation_begin = Clock::now();
        std::vector<Instance> instances;
        instances.reserve(count);
        for (int z = 0; z < depth; ++z) {
            for (int y = 0; y < side; ++y) {
                for (int x = 0; x < side; ++x) {
                    const float3 offset{12.0f * (x - side / 2), 6.0f * (y - side / 2), -4.0f * z};
                    const auto material_offset = static_cast<std::size_t>(x + y + z);
                    for (std::size_t i = 0; i < prototypes.size(); ++i) {
                        auto instance = prototypes[i];
                        instance.translation += offset;
                        instance.material = materials[(i + material_offset) % materials.size()];
                        instances.push_back(instance);
                    }
                }
            }
        }
        std::cout << "CPU instance generation: " << elapsed_ms(generation_begin) << " ms\n";
        const auto memory_before = free_gpu_memory();
        std::cout << "Preparing and uploading scene...\n" << std::flush;
        const auto preparation_begin = Clock::now();
        Scene scene(context, instances);
        std::cout << "Scene preparation + upload + creation (wall): " << elapsed_ms(preparation_begin) << " ms\n";
        const auto material_bytes = scene.material_count() * sizeof(SurfaceMaterial);
        const auto id_bytes = count * sizeof(std::uint32_t);
        const auto saved_bytes = static_cast<std::int64_t>(count * sizeof(SurfaceMaterial)) -
                                 static_cast<std::int64_t>(material_bytes + id_bytes);
        std::cout << "Material storage: " << scene.material_count() << " unique materials, "
                  << material_bytes << " palette bytes + " << id_bytes << " instance-ID bytes; saved "
                  << saved_bytes << " bytes (" << static_cast<float>(saved_bytes) / (1024 * 1024)
                  << " MiB) versus per-instance materials.\n";
        std::vector<Instance>().swap(instances);
        std::cout << "Building scene BVH...\n" << std::flush;
        const auto build_begin = Clock::now();
        scene.build();
        std::cout << "Scene BVH build (wall, including scratch allocation and GPU sync): "
                  << elapsed_ms(build_begin) << " ms\n";
        std::cout << "Scene BVH built: " << count << " instances (" << count * 3 / 8 << " spheres + "
                  << count / 8 << " diamond meshes + " << count * 2 / 8 << " PLY meshes + "
                  << count / 8 << " box meshes + " << count / 8 << " tori), 5 shared geometry BVHs.\n";
        const auto memory_after = free_gpu_memory();
        std::cout << "GPU memory change after scene setup (not peak, device-wide): "
                  << (static_cast<float>(memory_before) - static_cast<float>(memory_after)) / (1024 * 1024)
                  << " MiB\n";

        const Camera camera(small ? float3{0, 0, 10} : float3{18, 14, 30},
                            small ? float3{0, 0, 0} : float3{0, 0, -18},
                            {0, 1, 0}, small ? 42.0f : 55.0f);
        const PointLight light(small ? float3{-1, 4, 6} : float3{-15, 25, 30},
                               {1.0f, 0.95f, 0.85f}, small ? 65.0f : 1800.0f);
        std::cout << "Static scene; many instances are outside the view or occluded; camera far limit is 100.\n";
        render_scene(scene, camera, light, "scene.png", samples_per_pixel, integrator);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
