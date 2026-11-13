#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>

#include "ply_mesh.h"

namespace {
struct TraceScene {
    hiprtGeometry geometry;
    hiprtFuncTable functions;
    hiprtGeometry mesh;
    const float4* mesh_vertices;
    unsigned int mesh_triangle_count;
    unsigned int cat_detail_start;
    bool detail;
};
}

#include "renderer_common.h"

// The linked HIP RT device library calls these global dispatch functions.
__device__ bool intersectFunc(std::uint32_t geom_type, std::uint32_t ray_type, const hiprtFuncTableHeader& table,
                              const hiprtRay& ray, void*, hiprtHit& result) {
    const auto* detail_flag = static_cast<const unsigned int*>(
        table.funcDataSets[geom_type * table.numRayTypes + ray_type].intersectFuncData);
    const bool detail = detail_flag && *detail_flag != 0;
    const Ray scene_ray{ray.origin, ray.direction};
    Hit hit{};
    if (result.primID >= (detail ? detail_object_count : scene_object_count) ||
        !hit_primitive(scene_ray, result.primID, ray.maxT, hit, detail) ||
        hit.distance < ray.minT) {
        return false;
    }
    result.t = hit.distance;
    result.normal = hit.front_face ? hit.normal : -hit.normal;
    return true;
}

__device__ bool filterFunc(std::uint32_t, std::uint32_t, const hiprtFuncTableHeader&,
                           const hiprtRay&, void*, const hiprtHit&) {
    return false;
}

namespace {

constexpr std::array<float3, 6> mesh_points{{
    {0.0f, 1.35f, 1.45f}, {0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 1.95f},
    {-0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 0.95f}, {0.0f, 0.02f, 1.45f},
}};
constexpr std::array<uint3, 8> mesh_triangles{{
    {0, 2, 1}, {0, 3, 2}, {0, 4, 3}, {0, 1, 4},
    {5, 1, 2}, {5, 2, 3}, {5, 3, 4}, {5, 4, 1},
}};
constexpr float3 cat_position{1.85f, 0.0f, 1.75f};

__device__ bool hit_custom_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = false;
    float closest_distance = 1.0e30f;
    Hit candidate{};

    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = closest_distance;
    hiprtGeomCustomTraversalClosest traversal(
        scene.geometry, traversal_ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const hiprtHit result = traversal.getNextHit();
    if (result.hasHit()) {
        candidate.distance = result.t;
        candidate.point = ray.origin + result.t * ray.direction;
        set_face_normal(candidate, ray, result.normal);
        candidate.object_id = result.primID;
        set_object_material(candidate, result.primID, scene.detail);
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_custom_objects(ray, closest_hit, scene);
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = found ? closest_hit.distance : 1.0e30f;
    hiprtGeomTraversalClosest traversal(scene.mesh, traversal_ray);
    const hiprtHit result = traversal.getNextHit();
    if (result.hasHit()) {
        closest_hit.distance = result.t;
        closest_hit.point = ray.origin + result.t * ray.direction;
        set_face_normal(closest_hit, ray, normalize(result.normal));
        if (scene.detail) {
            closest_hit.object_id = scene_object_count + 1;
            closest_hit.albedo = {0.72f, 0.39f, 0.13f};
            closest_hit.material = Material::phong;
            closest_hit.roughness = 0.0f;
        } else if (result.primID < mesh_triangles.size()) {
            closest_hit.object_id = scene_object_count;
            closest_hit.albedo = {0.10f, 0.65f, 0.72f};
            closest_hit.material = Material::phong;
            closest_hit.roughness = 0.0f;
        } else {
            closest_hit.object_id = scene_object_count + 1;
            const bool detail = result.primID >= scene.cat_detail_start;
            closest_hit.albedo = detail ? float3{0.025f, 0.018f, 0.012f} : float3{0.78f, 0.36f, 0.16f};
            closest_hit.material = Material::diffuse;
            closest_hit.roughness = 1.0f;
        }
        found = true;
    }
    return found;
}

void check_hiprt(hiprtError result, std::string_view operation) {
    if (result != hiprtSuccess) {
        throw std::runtime_error(std::string(operation) + ": HIP RT error " + std::to_string(result));
    }
}

template <typename T>
std::unique_ptr<T, HipBufferDeleter> allocate_device(std::size_t count, std::string_view operation) {
    T* pointer = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&pointer), count * sizeof(T)), operation);
    return std::unique_ptr<T, HipBufferDeleter>(pointer);
}

void append_triangle(std::vector<float4>& vertices, float3 a, float3 b, float3 c) {
    vertices.push_back({a.x, a.y, a.z, 0.0f});
    vertices.push_back({b.x, b.y, b.z, 0.0f});
    vertices.push_back({c.x, c.y, c.z, 0.0f});
}

void append_ply_mesh(std::vector<float4>& vertices, const std::string& path, MeshUp mesh_up, bool detail = false) {
    const PlyMesh mesh = load_ply_mesh(path);
    const auto orient = [mesh_up](const std::array<float, 3>& point) {
        return mesh_up == MeshUp::z
            ? std::array<double, 3>{point[0], point[2], -static_cast<double>(point[1])}
            : std::array<double, 3>{point[0], point[1], point[2]};
    };
    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double, 3> maximum{-minimum[0], -minimum[1], -minimum[2]};
    for (const auto& triangle : mesh.triangles) {
        for (const auto index : triangle) {
            const auto point = orient(mesh.vertices[index]);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
        }
    }
    const std::array<double, 3> available = detail ? std::array<double, 3>{1.85, 2.45, 1.55}
                                                   : std::array<double, 3>{1.05, 1.55, 0.9};
    const float3 position = detail ? float3{0.0f, 0.0f, -0.65f} : cat_position;
    double scale = std::numeric_limits<double>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double extent = maximum[axis] - minimum[axis];
        if (extent > 0) {
            scale = std::min(scale, available[axis] / extent);
        }
    }
    if (!std::isfinite(scale)) {
        throw std::runtime_error("PLY '" + path + "': mesh has zero extent");
    }
    vertices.reserve(vertices.size() + mesh.triangles.size() * 3);
    for (const auto& triangle : mesh.triangles) {
        for (const auto index : triangle) {
            const auto point = orient(mesh.vertices[index]);
            vertices.push_back({
                static_cast<float>(position.x + (point[0] - (minimum[0] + maximum[0]) * 0.5) * scale),
                static_cast<float>(0.02 + (point[1] - minimum[1]) * scale),
                static_cast<float>(position.z + (point[2] - (minimum[2] + maximum[2]) * 0.5) * scale),
                0.0f});
        }
    }
    std::cout << "Loaded PLY mesh: " << path << " (" << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles).\n";
}

void append_ellipsoid(std::vector<float4>& vertices, float3 center, float3 radius) {
    constexpr float diagonal = 0.70710678f;
    constexpr std::array<float2, 8> ring{{
        {1.0f, 0.0f}, {diagonal, diagonal}, {0.0f, 1.0f}, {-diagonal, diagonal},
        {-1.0f, 0.0f}, {-diagonal, -diagonal}, {0.0f, -1.0f}, {diagonal, -diagonal},
    }};
    std::array<std::array<float3, 8>, 3> rings{};
    for (std::size_t row = 0; row < rings.size(); ++row) {
        const float y = row == 0 ? diagonal : row == 1 ? 0.0f : -diagonal;
        const float scale = row == 1 ? 1.0f : diagonal;
        for (std::size_t index = 0; index < ring.size(); ++index) {
            rings[row][index] = {center.x + radius.x * scale * ring[index].x,
                                 center.y + radius.y * y,
                                 center.z + radius.z * scale * ring[index].y};
        }
    }
    const float3 top{center.x, center.y + radius.y, center.z};
    const float3 bottom{center.x, center.y - radius.y, center.z};
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const std::size_t next = (index + 1) % ring.size();
        append_triangle(vertices, top, rings[0][next], rings[0][index]);
        for (std::size_t row = 0; row + 1 < rings.size(); ++row) {
            append_triangle(vertices, rings[row][index], rings[row][next], rings[row + 1][index]);
            append_triangle(vertices, rings[row][next], rings[row + 1][next], rings[row + 1][index]);
        }
        append_triangle(vertices, rings[2][index], rings[2][next], bottom);
    }
}

float3 cat_point(float x, float y, float z) {
    return {cat_position.x + x, y, cat_position.z + z};
}

void append_ear(std::vector<float4>& vertices, float side) {
    const float3 inner = cat_point(side * 0.07f, 1.37f, 0.13f);
    const float3 outer = cat_point(side * 0.27f, 1.32f, 0.10f);
    const float3 back = cat_point(side * 0.19f, 1.35f, -0.10f);
    const float3 tip = cat_point(side * 0.28f, 1.72f, 0.02f);
    append_triangle(vertices, inner, outer, tip);
    append_triangle(vertices, outer, back, tip);
    append_triangle(vertices, back, inner, tip);
    append_triangle(vertices, inner, back, outer);
}

void append_tail(std::vector<float4>& vertices) {
    constexpr std::array<float3, 5> points{{
        {0.23f, 0.16f, 0.0f}, {0.45f, 0.18f, 0.0f}, {0.61f, 0.35f, 0.0f},
        {0.63f, 0.59f, 0.0f}, {0.53f, 0.72f, 0.0f},
    }};
    constexpr std::array<float2, 6> ring{{
        {1.0f, 0.0f}, {0.5f, 0.8660254f}, {-0.5f, 0.8660254f},
        {-1.0f, 0.0f}, {-0.5f, -0.8660254f}, {0.5f, -0.8660254f},
    }};
    std::array<std::array<float3, 6>, points.size()> rings{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const float3 before = points[index == 0 ? 0 : index - 1];
        const float3 after = points[index + 1 == points.size() ? index : index + 1];
        const float dx = after.x - before.x;
        const float dy = after.y - before.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        for (std::size_t side = 0; side < ring.size(); ++side) {
            rings[index][side] = cat_point(points[index].x - 0.075f * dy / length * ring[side].x,
                                           points[index].y + 0.075f * dx / length * ring[side].x,
                                           points[index].z + 0.075f * ring[side].y);
        }
    }
    for (std::size_t side = 0; side < ring.size(); ++side) {
        const std::size_t next = (side + 1) % ring.size();
        append_triangle(vertices, cat_point(points.front().x, points.front().y, points.front().z),
                        rings.front()[side], rings.front()[next]);
        for (std::size_t index = 0; index + 1 < points.size(); ++index) {
            append_triangle(vertices, rings[index][side], rings[index + 1][side], rings[index][next]);
            append_triangle(vertices, rings[index][next], rings[index + 1][side], rings[index + 1][next]);
        }
        append_triangle(vertices, cat_point(points.back().x, points.back().y, points.back().z),
                        rings.back()[next], rings.back()[side]);
    }
}

struct HiprtContextOwner {
    hiprtContext value = nullptr;
    ~HiprtContextOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyContext(value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT context: error " << result << '\n';
            }
        }
    }
};

struct HiprtGeometryOwner {
    hiprtContext context = nullptr;
    hiprtGeometry value = nullptr;
    ~HiprtGeometryOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyGeometry(context, value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT geometry: error " << result << '\n';
            }
        }
    }
};

struct HiprtFunctionTableOwner {
    hiprtContext context = nullptr;
    hiprtFuncTable value = nullptr;
    ~HiprtFunctionTableOwner() {
        if (value) {
            const hiprtError result = hiprtDestroyFuncTable(context, value);
            if (result != hiprtSuccess) {
                std::cerr << "Destroying HIP RT function table: error " << result << '\n';
            }
        }
    }
};

class HiprtScene {
public:
    explicit HiprtScene(int device, const std::string& ply_path = {}, MeshUp mesh_up = MeshUp::y,
                        bool detail = false) : detail_(detail) {
        if (detail && ply_path.empty()) {
            throw std::runtime_error("The detail scene requires a PLY mesh.");
        }
        hipCtx_t hip_context = nullptr;
        check_hip(hipCtxGetCurrent(&hip_context), "Reading the current HIP context");
        if (!hip_context) {
            throw std::runtime_error("No current HIP context is available for HIP RT.");
        }
        hiprtContextCreationInput context_input{};
        context_input.ctxt = hip_context;
        context_input.device = device;
        context_input.deviceType = hiprtDeviceAMD;
        check_hiprt(hiprtCreateContext(HIPRT_API_VERSION, context_input, context_.value),
                    "Creating the HIP RT context");

        // Keep the sphere-traced torus tolerance inside the traversal bounds.
        constexpr float margin = 0.01f;
        std::array<float3, scene_object_count * 2> bounds{};
        const unsigned int object_count = detail ? detail_object_count : scene_object_count;
        for (unsigned int index = 0; index < object_count; ++index) {
            const SceneObject object = scene_object(index, detail);
            const float sphere_size = detail ? detail_sphere_radius : sphere_radius;
            const float major = detail ? detail_torus_major_radius : torus_major_radius;
            const float minor = detail ? detail_torus_minor_radius : torus_minor_radius;
            const float width = (object.sphere ? sphere_size : major + minor) + margin;
            const float absolute_sine = std::abs(object.tilt_sine);
            const float height = (object.sphere ? sphere_size : minor + major * absolute_sine) + margin;
            const float depth = (object.sphere ? sphere_size : minor + major * object.tilt_cosine) + margin;
            bounds[index * 2] = {object.center.x - width, object.center.y - height, object.center.z - depth};
            bounds[index * 2 + 1] = {object.center.x + width, object.center.y + height, object.center.z + depth};
        }
        bounds_ = allocate_device<float3>(bounds.size(), "Allocating HIP RT bounding boxes");
        check_hip(hipMemcpy(bounds_.get(), bounds.data(), sizeof(bounds), hipMemcpyHostToDevice),
                  "Uploading HIP RT bounding boxes");

        hiprtGeometryBuildInput input{};
        input.type = hiprtPrimitiveTypeAABBList;
        input.geomType = 0;
        input.primitive.aabbList.aabbs = bounds_.get();
        input.primitive.aabbList.aabbCount = object_count;
        input.primitive.aabbList.aabbStride = 2 * sizeof(float3);
        const hiprtBuildOptions options{};
        std::size_t temporary_size = 0;
        check_hiprt(hiprtGetGeometryBuildTemporaryBufferSize(context_.value, input, options, temporary_size),
                    "Querying HIP RT build memory");
        std::unique_ptr<std::uint8_t, HipBufferDeleter> temporary;
        if (temporary_size != 0) {
            temporary = allocate_device<std::uint8_t>(temporary_size, "Allocating HIP RT build memory");
        }
        geometry_.context = context_.value;
        check_hiprt(hiprtCreateGeometry(context_.value, input, options, geometry_.value),
                    "Creating HIP RT geometry");
        check_hiprt(hiprtBuildGeometry(context_.value, hiprtBuildOperationBuild, input, options,
                                      temporary.get(), nullptr, geometry_.value),
                    "Building HIP RT geometry");
        check_hip(hipDeviceSynchronize(), "Waiting for HIP RT geometry");

        functions_.context = context_.value;
        check_hiprt(hiprtCreateFuncTable(context_.value, 1, 1, functions_.value),
                    "Creating HIP RT function table");
        hiprtFuncDataSet function_data{};
        if (detail) {
            detail_flag_ = allocate_device<unsigned int>(1, "Allocating detail scene flag");
            const unsigned int flag = 1;
            check_hip(hipMemcpy(detail_flag_.get(), &flag, sizeof(flag), hipMemcpyHostToDevice),
                      "Uploading detail scene flag");
            function_data.intersectFuncData = detail_flag_.get();
        }
        check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, 0, 0, function_data),
                    "Initializing HIP RT function table");

        std::vector<float4> vertices;
        if (!detail) {
            for (const uint3 triangle : mesh_triangles) {
                append_triangle(vertices, mesh_points[triangle.x], mesh_points[triangle.y], mesh_points[triangle.z]);
            }
        }
        if (ply_path.empty()) {
            append_ellipsoid(vertices, cat_point(0.0f, 0.57f, 0.0f), {0.34f, 0.56f, 0.28f});
            append_ellipsoid(vertices, cat_point(0.0f, 1.20f, 0.0f), {0.31f, 0.31f, 0.26f});
            append_ear(vertices, -1.0f);
            append_ear(vertices, 1.0f);
            append_tail(vertices);
            cat_detail_start_ = static_cast<unsigned int>(vertices.size() / 3);
            append_ellipsoid(vertices, cat_point(-0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
            append_ellipsoid(vertices, cat_point(0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
            append_ellipsoid(vertices, cat_point(0.0f, 1.14f, 0.255f), {0.035f, 0.025f, 0.025f});
        } else {
            append_ply_mesh(vertices, ply_path, mesh_up, detail);
            cat_detail_start_ = static_cast<unsigned int>(vertices.size() / 3);
        }
        mesh_triangle_count_ = static_cast<unsigned int>(vertices.size() / 3);
        mesh_vertices_ = allocate_device<float4>(vertices.size(), "Allocating mesh vertices");
        check_hip(hipMemcpy(mesh_vertices_.get(), vertices.data(), vertices.size() * sizeof(float4), hipMemcpyHostToDevice),
                  "Uploading mesh vertices");
        hiprtGeometryBuildInput mesh_input{};
        mesh_input.type = hiprtPrimitiveTypeTriangleMesh;
        mesh_input.primitive.triangleMesh.vertices = mesh_vertices_.get();
        mesh_input.primitive.triangleMesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
        mesh_input.primitive.triangleMesh.vertexStride = sizeof(float4);
        mesh_input.primitive.triangleMesh.triangleCount = mesh_triangle_count_;
        hiprtBuildOptions mesh_options{};
        mesh_options.buildFlags = hiprtBuildFlagBitDisableTrianglePairing;
        std::size_t mesh_temporary_size = 0;
        check_hiprt(hiprtGetGeometryBuildTemporaryBufferSize(context_.value, mesh_input, mesh_options,
                                                             mesh_temporary_size),
                    "Querying HIP RT mesh build memory");
        std::unique_ptr<std::uint8_t, HipBufferDeleter> mesh_temporary;
        if (mesh_temporary_size != 0) {
            mesh_temporary = allocate_device<std::uint8_t>(mesh_temporary_size, "Allocating HIP RT mesh build memory");
        }
        mesh_.context = context_.value;
        check_hiprt(hiprtCreateGeometry(context_.value, mesh_input, mesh_options, mesh_.value),
                    "Creating HIP RT mesh geometry");
        check_hiprt(hiprtBuildGeometry(context_.value, hiprtBuildOperationBuild, mesh_input, mesh_options,
                                      mesh_temporary.get(), nullptr, mesh_.value),
                    "Building HIP RT mesh geometry");
        check_hip(hipDeviceSynchronize(), "Waiting for HIP RT mesh geometry");
    }

    TraceScene scene() const {
        return {geometry_.value, functions_.value, mesh_.value, mesh_vertices_.get(),
                mesh_triangle_count_, cat_detail_start_, detail_};
    }

private:
    HiprtContextOwner context_;
    std::unique_ptr<float3, HipBufferDeleter> bounds_;
    HiprtGeometryOwner geometry_;
    std::unique_ptr<unsigned int, HipBufferDeleter> detail_flag_;
    HiprtFunctionTableOwner functions_;
    std::unique_ptr<float4, HipBufferDeleter> mesh_vertices_;
    HiprtGeometryOwner mesh_;
    unsigned int mesh_triangle_count_ = 0;
    unsigned int cat_detail_start_ = 0;
    bool detail_ = false;
};

}
