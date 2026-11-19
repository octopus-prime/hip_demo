#pragma once

#include "geometry.h"
#include "function_table.h"

#include <span>
#include <cstdint>

struct SurfaceMaterial {
    float3 base_color{0.8f, 0.8f, 0.8f};
    float specular_strength = 0.0f;
    float shininess = 32.0f;
    float reflectivity = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
};

struct Instance {
    const Geometry& geometry;
    float3 translation{0.0f, 0.0f, 0.0f};
    float3 rotation_axis{0.0f, 0.0f, 1.0f};
    float rotation_degrees = 0.0f;
    float3 scale{1.0f, 1.0f, 1.0f};
    SurfaceMaterial material;
    float3 emission{0, 0, 0};
};

struct AreaLight {
    std::uint32_t instance_id;
    float3 radiance;
};

class Scene {
public:
    Scene(const Context& context, std::span<const Instance> instances);
    ~Scene() noexcept;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void build();
    std::optional<hiprtHit> trace(const hiprtRay& ray) const;
    hiprtScene get() const;
    hiprtFuncTable functions() const noexcept { return functions_.get(); }
    const SurfaceMaterial* materials() const noexcept { return materials_.get(); }
    const std::uint32_t* material_ids() const noexcept { return material_ids_.get(); }
    std::size_t material_count() const noexcept { return material_count_; }
    bool has_transmission() const noexcept { return has_transmission_; }
    const AreaLight* area_lights() const noexcept { return area_lights_.get(); }
    unsigned int area_light_count() const noexcept { return area_light_count_; }

private:
    struct Materials;
    static Materials prepare_materials(std::span<const Instance> instances);
    Scene(const Context& context, std::span<const Instance> instances, Materials materials);

    const Context& context_;
    DeviceBuffer<hiprtInstance> instances_;
    DeviceBuffer<hiprtFrameSRT> frames_;
    DeviceBuffer<SurfaceMaterial> materials_;
    DeviceBuffer<std::uint32_t> material_ids_;
    std::size_t material_count_;
    bool has_transmission_;
    DeviceBuffer<AreaLight> area_lights_;
    unsigned int area_light_count_;
    FunctionTable functions_;
    hiprtSceneBuildInput input_{};
    hiprtScene scene_ = nullptr;
    bool built_ = false;
};
