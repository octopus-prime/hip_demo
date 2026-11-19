#include "geometry.h"
#include "function_table.h"
#include "../src/math/torus_roots.h"

#include <hiprt/hiprt_device.h>
#include <hiprt/hiprt_math.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <array>
#include <cmath>
#include <limits>

// HIP RT dispatches AABB candidates here in geometry-local coordinates.
__device__ bool intersectFunc(std::uint32_t geom_type, std::uint32_t ray_type, const hiprtFuncTableHeader& table,
                              const hiprtRay& ray, void*, hiprtHit& hit)
{
    if (geom_type != sphere_geometry_type) {
        const auto* torus = static_cast<const Torus*>(
            table.funcDataSets[geom_type * table.numRayTypes + ray_type].intersectFuncData);
        const auto roots = hip_demo::torus_roots(ray.origin, ray.direction, torus->major_radius,
                                                torus->minor_radius, ray.minT, ray.maxT);
        for (int i = 0; i < roots.roots.count; ++i) {
            const float t = roots.distance(i);
            if (t < ray.minT || t > ray.maxT) continue;
            hit.t = t;
            hit.normal = roots.normal(i);
            hit.uv = {0, 0};
            return true;
        }
        return false;
    }
    // Do not assume a unit direction: instance scaling changes its length.
    const float a = hiprt::dot(ray.direction, ray.direction);
    const float b = 2.0f * hiprt::dot(ray.origin, ray.direction);
    const float c = hiprt::dot(ray.origin, ray.origin) - 1.0f;
    if (a <= 0.0f) return false;
    const auto roots = hip_demo::quadratic_roots(a, b, c);
    for (int i = 0; i < roots.count; ++i) {
        const float t = roots.values[i];
        if (t < ray.minT || t > ray.maxT) continue;
        hit.t = t;
        hit.normal = ray.origin + t * ray.direction;
        hit.uv = {0, 0};
        return true;
    }
    return false;
}

// Required by the HIP RT device library. False means: do not discard the hit.
__device__ bool filterFunc(std::uint32_t, std::uint32_t, const hiprtFuncTableHeader&,
                           const hiprtRay&, void*, const hiprtHit&)
{
    return false;
}

namespace {

constexpr std::array<float3, 6> plane_vertices{{
    {-1, 0, 1}, {1, 0, 1}, {1, 0, -1},
    {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
}};

constexpr std::array<float3, 36> box_vertices{{
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
    { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1,-1,-1}, {-1, 1,-1}, { 1, 1,-1},
    { 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1,-1, 1}, { 1, 1,-1}, { 1, 1, 1},
    {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1,-1,-1}, {-1, 1, 1}, {-1, 1,-1},
    {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1, 1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1,-1}, { 1,-1, 1}, {-1,-1, 1},
}};

std::size_t torus_count(Torus torus)
{
    const float outer = torus.major_radius + torus.minor_radius;
    if (!std::isfinite(torus.major_radius) || !std::isfinite(torus.minor_radius) ||
        torus.minor_radius <= 0 || torus.major_radius <= torus.minor_radius ||
        !std::isfinite(outer * outer) || outer * outer <= 0 || torus.minor_radius * torus.minor_radius <= 0 ||
        outer <= torus.major_radius || torus.major_radius - torus.minor_radius >= torus.major_radius) {
        throw std::invalid_argument("Torus requires finite radii with 0 < minor < major and representable float bounds/squares.");
    }
    return 1;
}

std::size_t mesh_vertex_count(std::span<const float3> vertices)
{
    if (vertices.empty() || vertices.size() % 3 != 0 ||
        vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Mesh must contain complete triangles and between 3 and UINT32_MAX vertices.");
    }
    for (const auto& vertex : vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) {
            throw std::invalid_argument("Mesh vertices must be finite.");
        }
    }
    return vertices.size();
}

__global__ void trace_ray(hiprtGeometry geometry, hiprtFuncTable functions, bool custom,
                           hiprtRay ray, hiprtHit* result)
{
    if (custom) {
        hiprtGeomCustomTraversalClosest traversal(geometry, ray, hiprtTraversalHintDefault, nullptr, functions);
        *result = traversal.getNextHit();
    } else {
        hiprtGeomTraversalClosest traversal(geometry, ray);
        *result = traversal.getNextHit();
    }
}

}

Geometry::Geometry(const Context& context, std::span<const float3> vertices)
    : context_(context), primitive_data_(mesh_vertex_count(vertices))
{
    primitive_data_.upload(vertices);

    input_.type = hiprtPrimitiveTypeTriangleMesh;
    input_.primitive.triangleMesh.vertices = primitive_data_.get();
    input_.primitive.triangleMesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
    input_.primitive.triangleMesh.vertexStride = sizeof(float3);
    input_.primitive.triangleMesh.triangleCount = static_cast<std::uint32_t>(vertices.size() / 3);
    // Without indices, HIP RT takes each consecutive group of three vertices.

    const hiprtBuildOptions options{};
    check_hiprt(hiprtCreateGeometry(context_.get(), input_, options, geometry_),
                "Creating mesh geometry");
}

Geometry::Geometry(const Context& context, UnitSphere)
    : context_(context), primitive_data_(2)
{
    const std::array<float3, 2> bounds{{{-1, -1, -1}, {1, 1, 1}}};
    primitive_data_.upload(bounds);
    input_.type = hiprtPrimitiveTypeAABBList;
    input_.geomType = sphere_geometry_type;
    input_.primitive.aabbList.aabbs = primitive_data_.get();
    input_.primitive.aabbList.aabbCount = 1;
    input_.primitive.aabbList.aabbStride = 2 * sizeof(float3);
    const hiprtBuildOptions options{};
    check_hiprt(hiprtCreateGeometry(context_.get(), input_, options, geometry_),
                "Creating sphere geometry");
}

Geometry::Geometry(const Context& context, UnitBox)
    : Geometry(context, box_vertices)
{
}

Geometry::Geometry(const Context& context, UnitPlane)
    : Geometry(context, plane_vertices)
{
    is_plane_ = true;
}

Geometry::Geometry(const Context& context, Torus torus)
    : context_(context), primitive_data_(2), torus_data_(torus_count(torus)),
      geometry_type_(context.allocate_geometry_type())
{
    torus_data_.upload(std::span<const Torus>(&torus, 1));
    const float outer = torus.major_radius + torus.minor_radius;
    const std::array<float3, 2> bounds{{
        {-outer, -torus.minor_radius, -outer}, {outer, torus.minor_radius, outer},
    }};
    primitive_data_.upload(bounds);
    input_.type = hiprtPrimitiveTypeAABBList;
    input_.geomType = geometry_type_;
    input_.primitive.aabbList.aabbs = primitive_data_.get();
    input_.primitive.aabbList.aabbCount = 1;
    input_.primitive.aabbList.aabbStride = 2 * sizeof(float3);
    const hiprtBuildOptions options{};
    check_hiprt(hiprtCreateGeometry(context_.get(), input_, options, geometry_),
                "Creating torus geometry");
}

Geometry::~Geometry() noexcept
{
    const hiprtError result = hiprtDestroyGeometry(context_.get(), geometry_);
    if (result != hiprtSuccess) {
        std::cerr << "Destroying geometry: HIP RT error " << result << '\n';
    }
}

void Geometry::build()
{
    built_ = false;
    const hiprtBuildOptions options{};
    std::size_t temporary_size = 0;
    check_hiprt(hiprtGetGeometryBuildTemporaryBufferSize(context_.get(), input_, options, temporary_size),
                "Querying BVH build memory");
    DeviceBuffer<std::byte> temporary(temporary_size);

    check_hiprt(hiprtBuildGeometry(context_.get(), hiprtBuildOperationBuild, input_, options,
                                 temporary.get(), nullptr, geometry_),
                "Building geometry BVH");
    // Building is asynchronous: finish before releasing the temporary GPU memory.
    check_hip(hipDeviceSynchronize(), "Waiting for geometry BVH build");
    built_ = true;
}

std::optional<hiprtHit> Geometry::trace(const hiprtRay& ray) const
{
    const hiprtGeometry handle = get();
    FunctionTable functions(context_, geometry_type_ + 1);
    functions.set(geometry_type_, intersection_data());
    DeviceBuffer<hiprtHit> result(1);
    // One block with one thread: this tutorial traces just one ray per call.
    hipLaunchKernelGGL(trace_ray, dim3(1), dim3(1), 0, 0, handle, functions.get(),
                      input_.type == hiprtPrimitiveTypeAABBList, ray, result.get());
    check_hip(hipGetLastError(), "Launching ray query");
    check_hip(hipDeviceSynchronize(), "Waiting for ray query");

    hiprtHit hit{};
    result.download(std::span<hiprtHit>(&hit, 1));
    if (hit.primID == hiprtInvalidValue) {
        return std::nullopt;
    }
    return hit;
}

hiprtGeometry Geometry::get() const
{
    if (!built_) {
        throw std::logic_error("Build the geometry before tracing rays.");
    }
    return geometry_;
}
