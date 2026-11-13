#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>

namespace {
struct TraceScene {
    hiprtGeometry geometry;
    hiprtFuncTable functions;
    hiprtGeometry mesh;
    const float4* mesh_vertices;
};
}

#include "renderer_common.h"

// The linked HIP RT device library calls these global dispatch functions.
__device__ bool intersectFunc(std::uint32_t, std::uint32_t, const hiprtFuncTableHeader&,
                              const hiprtRay& ray, void*, hiprtHit& result) {
    const Ray scene_ray{ray.origin, ray.direction};
    Hit hit{};
    if (result.primID >= scene_object_count ||
        !hit_primitive(scene_ray, result.primID, ray.maxT, hit) ||
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
        set_object_material(candidate, result.primID);
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
        closest_hit.object_id = scene_object_count;
        closest_hit.albedo = {0.10f, 0.65f, 0.72f};
        closest_hit.material = Material::phong;
        closest_hit.roughness = 0.0f;
        found = true;
    }
    return found;
}

constexpr std::array<float3, 6> mesh_points{{
    {0.0f, 1.35f, 1.45f}, {0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 1.95f},
    {-0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 0.95f}, {0.0f, 0.02f, 1.45f},
}};
constexpr std::array<uint3, 8> mesh_triangles{{
    {0, 2, 1}, {0, 3, 2}, {0, 4, 3}, {0, 1, 4},
    {5, 1, 2}, {5, 2, 3}, {5, 3, 4}, {5, 4, 1},
}};

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
    explicit HiprtScene(int device) {
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
        for (unsigned int index = 0; index < scene_object_count; ++index) {
            const SceneObject object = scene_object(index);
            const float width = (object.sphere ? sphere_radius : torus_major_radius + torus_minor_radius) + margin;
            const float absolute_sine = std::abs(object.tilt_sine);
            const float height = (object.sphere ? sphere_radius : torus_minor_radius + torus_major_radius * absolute_sine) + margin;
            const float depth = (object.sphere ? sphere_radius : torus_minor_radius + torus_major_radius * object.tilt_cosine) + margin;
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
        input.primitive.aabbList.aabbCount = scene_object_count;
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
        check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, 0, 0, {}),
                    "Initializing HIP RT function table");

        std::array<float4, mesh_triangles.size() * 3> vertices{};
        for (std::size_t index = 0; index < mesh_triangles.size(); ++index) {
            const uint3 triangle = mesh_triangles[index];
            const float3 a = mesh_points[triangle.x];
            const float3 b = mesh_points[triangle.y];
            const float3 c = mesh_points[triangle.z];
            vertices[index * 3] = {a.x, a.y, a.z, 0.0f};
            vertices[index * 3 + 1] = {b.x, b.y, b.z, 0.0f};
            vertices[index * 3 + 2] = {c.x, c.y, c.z, 0.0f};
        }
        mesh_vertices_ = allocate_device<float4>(vertices.size(), "Allocating mesh vertices");
        check_hip(hipMemcpy(mesh_vertices_.get(), vertices.data(), sizeof(vertices), hipMemcpyHostToDevice),
                  "Uploading mesh vertices");
        hiprtGeometryBuildInput mesh_input{};
        mesh_input.type = hiprtPrimitiveTypeTriangleMesh;
        mesh_input.primitive.triangleMesh.vertices = mesh_vertices_.get();
        mesh_input.primitive.triangleMesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
        mesh_input.primitive.triangleMesh.vertexStride = sizeof(float4);
        mesh_input.primitive.triangleMesh.triangleCount = static_cast<std::uint32_t>(mesh_triangles.size());
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
        return {geometry_.value, functions_.value, mesh_.value, mesh_vertices_.get()};
    }

private:
    HiprtContextOwner context_;
    std::unique_ptr<float3, HipBufferDeleter> bounds_;
    HiprtGeometryOwner geometry_;
    HiprtFunctionTableOwner functions_;
    std::unique_ptr<float4, HipBufferDeleter> mesh_vertices_;
    HiprtGeometryOwner mesh_;
};

}
