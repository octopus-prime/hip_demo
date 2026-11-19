#pragma once

#include "context.h"
#include "device_buffer.h"

#include <optional>
#include <span>

struct UnitSphere {};
struct UnitBox {};
struct UnitPlane {};
struct Torus {
    float major_radius = 1.0f;
    float minor_radius = 0.25f;
};

inline constexpr unsigned int sphere_geometry_type = 0;

class Geometry {
public:
    Geometry(const Context& context, std::span<const float3> vertices);
    Geometry(const Context& context, UnitSphere);
    Geometry(const Context& context, UnitBox);
    Geometry(const Context& context, UnitPlane);
    Geometry(const Context& context, Torus torus);
    ~Geometry() noexcept;

    Geometry(const Geometry&) = delete;
    Geometry& operator=(const Geometry&) = delete;

    void build();
    std::optional<hiprtHit> trace(const hiprtRay& ray) const;
    hiprtGeometry get() const;
    hiprtContext context() const noexcept { return context_.get(); }
    unsigned int geometry_type() const noexcept { return geometry_type_; }
    const void* intersection_data() const noexcept { return torus_data_.get(); }
    bool is_plane() const noexcept { return is_plane_; }

private:
    const Context& context_;
    DeviceBuffer<float3> primitive_data_;
    DeviceBuffer<Torus> torus_data_{0};
    unsigned int geometry_type_ = sphere_geometry_type;
    hiprtGeometryBuildInput input_{};
    hiprtGeometry geometry_ = nullptr;
    bool built_ = false;
    bool is_plane_ = false;
};
