#include "scene.h"

#include <hiprt/hiprt_device.h>
#include <hiprt/hiprt_math.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>
#include <array>
#include <functional>
#include <unordered_map>

namespace {

using MaterialKey = std::array<float, 8>;

struct MaterialHash {
    std::size_t operator()(const MaterialKey& key) const noexcept
    {
        std::size_t result = 0;
        for (const float value : key) {
            result ^= std::hash<float>{}(value) + 0x9e3779b9u + (result << 6) + (result >> 2);
        }
        return result;
    }
};

std::size_t instance_count(std::span<const Instance> instances)
{
    if (instances.empty() || instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Scene must contain between 1 and UINT32_MAX instances.");
    }
    return instances.size();
}

__global__ void trace_scene_ray(hiprtScene scene, hiprtFuncTable functions, hiprtRay ray, hiprtHit* result)
{
    hiprtSceneTraversalClosest traversal(scene, ray, hiprtFullRayMask, hiprtTraversalHintDefault,
                                         nullptr, functions);
    *result = traversal.getNextHit();
}

}

struct Scene::Materials {
    std::vector<SurfaceMaterial> palette;
    std::vector<std::uint32_t> ids;
    bool has_transmission = false;
    std::vector<AreaLight> area_lights;
};

Scene::Materials Scene::prepare_materials(std::span<const Instance> instances)
{
    Materials result;
    result.ids.reserve(instance_count(instances));
    std::unordered_map<MaterialKey, std::uint32_t, MaterialHash> indices;
    for (const auto& instance : instances) {
        const auto emission = instance.emission;
        if (emission.x != 0 || emission.y != 0 || emission.z != 0) {
            if (!std::isfinite(emission.x) || !std::isfinite(emission.y) || !std::isfinite(emission.z) ||
                emission.x < 0 || emission.y < 0 || emission.z < 0) {
                throw std::invalid_argument("Emission must be finite and nonnegative.");
            }
            if (!instance.geometry.is_plane()) {
                throw std::invalid_argument("Area-light emission requires UnitPlane geometry.");
            }
            const float area = static_cast<float>(4.0 * instance.scale.x * instance.scale.z);
            const float cross_squared = (area * 0.25f) * (area * 0.25f);
            if (!std::isfinite(area) || area <= 0 ||
                !std::isfinite(cross_squared) || cross_squared <= 0) {
                throw std::invalid_argument("Area-light area and squared cross product must be representable.");
            }
            result.area_lights.push_back({static_cast<std::uint32_t>(result.ids.size()), emission});
        }
        const auto& material = instance.material;
        const auto color = material.base_color;
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
            color.x < 0.0f || color.y < 0.0f || color.z < 0.0f ||
            color.x > 1.0f || color.y > 1.0f || color.z > 1.0f) {
            throw std::invalid_argument("Instance base color must be finite and in [0, 1].");
        }
        if (!std::isfinite(material.specular_strength) ||
            material.specular_strength < 0.0f || material.specular_strength > 1.0f) {
            throw std::invalid_argument("Specular strength must be finite and in [0, 1].");
        }
        if (!std::isfinite(material.shininess) || material.shininess <= 0.0f) {
            throw std::invalid_argument("Shininess must be finite and positive.");
        }
        if (!std::isfinite(material.reflectivity) ||
            material.reflectivity < 0.0f || material.reflectivity > 1.0f) {
            throw std::invalid_argument("Reflectivity must be finite and in [0, 1].");
        }
        if (!std::isfinite(material.transmission) ||
            material.transmission < 0.0f || material.transmission > 1.0f) {
            throw std::invalid_argument("Transmission must be finite and in [0, 1].");
        }
        if (!std::isfinite(material.ior) || material.ior < 1.0f) {
            throw std::invalid_argument("Index of refraction must be finite and at least 1.");
        }
        const MaterialKey key{color.x, color.y, color.z, material.specular_strength,
                              material.shininess, material.reflectivity, material.transmission, material.ior};
        const auto [entry, inserted] = indices.try_emplace(key, static_cast<std::uint32_t>(result.palette.size()));
        if (inserted) result.palette.push_back(material);
        result.ids.push_back(entry->second);
        result.has_transmission = result.has_transmission || material.transmission > 0;
    }
    return result;
}

Scene::Scene(const Context& context, std::span<const Instance> instances)
    : Scene(context, instances, prepare_materials(instances))
{
}

Scene::Scene(const Context& context, std::span<const Instance> instances, Materials materials)
    : context_(context), instances_(instances.size()), frames_(instances.size()),
      materials_(materials.palette.size()), material_ids_(materials.ids.size()),
      material_count_(materials.palette.size()), has_transmission_(materials.has_transmission),
      area_lights_(materials.area_lights.size()),
      area_light_count_(static_cast<unsigned int>(materials.area_lights.size())),
      functions_(context, context.geometry_type_count())
{
    std::vector<hiprtInstance> references;
    std::vector<hiprtFrameSRT> transforms;
    references.reserve(instances.size());
    transforms.reserve(instances.size());
    std::vector<bool> registered(context.geometry_type_count());
    for (const auto& instance : instances) {
        const auto translation = instance.translation;
        if (!std::isfinite(translation.x) || !std::isfinite(translation.y) || !std::isfinite(translation.z)) {
            throw std::invalid_argument("Instance translation must be finite.");
        }
        const auto scale = instance.scale;
        if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
            scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
            throw std::invalid_argument("Instance scale must be finite and positive.");
        }
        const float axis_length_squared = hiprt::dot(instance.rotation_axis, instance.rotation_axis);
        if (!std::isfinite(axis_length_squared) || axis_length_squared <= 0.0f) {
            throw std::invalid_argument("Instance rotation axis must have a finite, nonzero squared length.");
        }
        if (!std::isfinite(instance.rotation_degrees)) {
            throw std::invalid_argument("Instance rotation angle must be finite.");
        }
        const auto axis = hiprt::normalize(instance.rotation_axis);
        const float radians = std::remainder(instance.rotation_degrees, 360.0f) *
                              (std::numbers::pi_v<float> / 180.0f);

        hiprtInstance reference{};
        reference.type = hiprtInstanceTypeGeometry;
        if (instance.geometry.context() != context_.get()) {
            throw std::invalid_argument("Scene geometries must use the same HIP RT context.");
        }
        reference.geometry = instance.geometry.get();
        const auto type = instance.geometry.geometry_type();
        if (!registered[type]) {
            functions_.set(type, instance.geometry.intersection_data());
            registered[type] = true;
        }
        references.push_back(reference);

        hiprtFrameSRT frame{};
        frame.rotation = {axis.x, axis.y, axis.z, radians};
        frame.scale = scale;
        frame.translation = translation;
        transforms.push_back(frame);
    }
    instances_.upload(references);
    frames_.upload(transforms);
    materials_.upload(materials.palette);
    material_ids_.upload(materials.ids);
    area_lights_.upload(materials.area_lights);

    input_.instances = instances_.get();
    input_.instanceFrames = frames_.get();
    input_.instanceCount = static_cast<std::uint32_t>(instances.size());
    input_.frameCount = input_.instanceCount;
    // Null transform headers mean one frame per instance; null masks enable all rays.
    input_.frameType = hiprtFrameTypeSRT;
    const hiprtBuildOptions options{};
    check_hiprt(hiprtCreateScene(context_.get(), input_, options, scene_), "Creating HIP RT scene");
}

Scene::~Scene() noexcept
{
    const hiprtError result = hiprtDestroyScene(context_.get(), scene_);
    if (result != hiprtSuccess) {
        std::cerr << "Destroying HIP RT scene: HIP RT error " << result << '\n';
    }
}

void Scene::build()
{
    built_ = false;
    const hiprtBuildOptions options{};
    std::size_t temporary_size = 0;
    check_hiprt(hiprtGetSceneBuildTemporaryBufferSize(context_.get(), input_, options, temporary_size),
                "Querying scene BVH build memory");
    DeviceBuffer<std::byte> temporary(temporary_size);
    check_hiprt(hiprtBuildScene(context_.get(), hiprtBuildOperationBuild, input_, options,
                              temporary.get(), nullptr, scene_),
                "Building scene BVH");
    check_hip(hipDeviceSynchronize(), "Waiting for scene BVH build");
    built_ = true;
}

std::optional<hiprtHit> Scene::trace(const hiprtRay& ray) const
{
    const hiprtScene handle = get();
    DeviceBuffer<hiprtHit> result(1);
    hipLaunchKernelGGL(trace_scene_ray, dim3(1), dim3(1), 0, 0, handle, functions(), ray, result.get());
    check_hip(hipGetLastError(), "Launching scene ray query");
    check_hip(hipDeviceSynchronize(), "Waiting for scene ray query");

    hiprtHit hit{};
    result.download(std::span<hiprtHit>(&hit, 1));
    if (hit.primID == hiprtInvalidValue) {
        return std::nullopt;
    }
    return hit;
}

hiprtScene Scene::get() const
{
    if (!built_) {
        throw std::logic_error("Build the scene before tracing rays.");
    }
    return scene_;
}
