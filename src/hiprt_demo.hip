#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>

namespace {
struct TraceScene {
    hiprtGeometry geometry;
    hiprtFuncTable functions;
};
}

#include "renderer_common.h"

// The linked HIP RT device library calls these global dispatch functions.
__device__ bool intersectFunc(std::uint32_t, std::uint32_t, const hiprtFuncTableHeader&,
                              const hiprtRay& ray, void*, hiprtHit& result) {
    const Ray scene_ray{ray.origin, ray.direction};
    Hit hit{};
    bool found = false;
    if (result.primID == 0) {
        found = hit_sphere(scene_ray, sphere_center, sphere_radius, ray.maxT, hit);
    } else if (result.primID == 1) {
        found = hit_torus(scene_ray, ray.maxT, hit);
    }
    if (!found || hit.distance < ray.minT) {
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

__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
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
        if (result.primID == 0) {
            set_sphere_material(candidate);
        } else {
            set_torus_material(candidate);
        }
        closest_hit = candidate;
        found = true;
    }
    return found;
}

struct IntersectionCheck {
    bool expected_hit;
    bool actual_hit;
    float expected_distance;
    float actual_distance;
    Material expected_material;
    Material actual_material;
    bool expected_front_face;
    bool actual_front_face;
};

constexpr unsigned int intersection_check_count = 8 + 64 * 48;

__global__ void check_intersections(TraceScene scene, IntersectionCheck* results) {
    const Ray rays[] = {
        {{0.0f, 1.9f, 5.8f}, normalize(float3{-0.9f, 0.86f, 0.0f} - float3{0.0f, 1.9f, 5.8f})},
        {{0.0f, 1.9f, 5.8f}, normalize(float3{0.95f, 1.0f, 0.58f} - float3{0.0f, 1.9f, 5.8f})},
        {{0.0f, 1.9f, 5.8f}, normalize(float3{0.95f, 1.0f, 0.0f} - float3{0.0f, 1.9f, 5.8f})},
        {{0.0f, 1.9f, 5.8f}, {0.0f, 1.0f, 0.0f}},
        {{-0.9f, 0.86f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{1.53f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{-3.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{3.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    };
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= intersection_check_count) {
        return;
    }
    Ray ray{};
    if (index < sizeof(rays) / sizeof(rays[0])) {
        ray = rays[index];
    } else {
        const unsigned int pixel = index - 8;
        const float3 origin{0.0f, 1.9f, 5.8f};
        const float3 forward = normalize(float3{0.0f, 0.9f, 0.0f} - origin);
        const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const float3 up = cross(right, forward);
        const float u = (2.0f * (static_cast<float>(pixel % 64) + 0.5f) / 64 - 1.0f) *
            (4.0f / 3.0f) * 0.41421356f;
        const float v = (1.0f - 2.0f * (static_cast<float>(pixel / 64) + 0.5f) / 48) *
            0.41421356f;
        ray = {origin, normalize(forward + u * right + v * up)};
    }
    Hit expected{}, candidate{}, actual{};
    bool expected_hit = hit_sphere(ray, sphere_center, sphere_radius, 1.0e30f, expected);
    if (hit_torus(ray, expected_hit ? expected.distance : 1.0e30f, candidate)) {
        expected = candidate;
        expected_hit = true;
    }
    const bool actual_hit = hit_objects(ray, actual, scene);
    results[index] = {expected_hit, actual_hit, expected.distance, actual.distance,
                      expected.material, actual.material, expected.front_face, actual.front_face};
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
        constexpr float sphere_extent = sphere_radius + margin;
        constexpr float torus_extent = torus_major_radius + torus_minor_radius + margin;
        constexpr float torus_height = torus_minor_radius + margin;
        const std::array<float3, 4> bounds{{
            {sphere_center.x - sphere_extent, sphere_center.y - sphere_extent, sphere_center.z - sphere_extent},
            {sphere_center.x + sphere_extent, sphere_center.y + sphere_extent, sphere_center.z + sphere_extent},
            {torus_center.x - torus_extent, torus_center.y - torus_height, torus_center.z - torus_extent},
            {torus_center.x + torus_extent, torus_center.y + torus_height, torus_center.z + torus_extent},
        }};
        bounds_ = allocate_device<float3>(bounds.size(), "Allocating HIP RT bounding boxes");
        check_hip(hipMemcpy(bounds_.get(), bounds.data(), sizeof(bounds), hipMemcpyHostToDevice),
                  "Uploading HIP RT bounding boxes");

        hiprtGeometryBuildInput input{};
        input.type = hiprtPrimitiveTypeAABBList;
        input.geomType = 0;
        input.primitive.aabbList.aabbs = bounds_.get();
        input.primitive.aabbList.aabbCount = 2;
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

        auto device_results = allocate_device<IntersectionCheck>(intersection_check_count, "Allocating intersection checks");
        hipLaunchKernelGGL(check_intersections, dim3((intersection_check_count + 127) / 128),
                           dim3(128), 0, 0, scene(), device_results.get());
        check_hip(hipGetLastError(), "Launching HIP RT intersection checks");
        std::array<IntersectionCheck, intersection_check_count> results{};
        check_hip(hipMemcpy(results.data(), device_results.get(), results.size() * sizeof(results[0]), hipMemcpyDeviceToHost),
                  "Reading HIP RT intersection checks");
        for (std::size_t index = 0; index < results.size(); ++index) {
            const IntersectionCheck& result = results[index];
            const float distance_tolerance = result.expected_material == Material::glass ? 0.002f : 0.0001f;
            if (result.expected_hit != result.actual_hit ||
                (result.expected_hit && (!std::isfinite(result.actual_distance) ||
                 std::abs(result.expected_distance - result.actual_distance) > distance_tolerance ||
                 result.expected_material != result.actual_material ||
                 result.expected_front_face != result.actual_front_face))) {
                throw std::runtime_error("HIP RT intersection check failed for ray " + std::to_string(index) +
                                         " (expected hit " + std::to_string(result.expected_hit) +
                                         " at " + std::to_string(result.expected_distance) +
                                         ", got hit " + std::to_string(result.actual_hit) +
                                         " at " + std::to_string(result.actual_distance) + ")");
            }
        }
        std::cout << "HIP RT custom primitive intersection checks passed.\n";
    }

    TraceScene scene() const { return {geometry_.value, functions_.value}; }

private:
    HiprtContextOwner context_;
    std::unique_ptr<float3, HipBufferDeleter> bounds_;
    HiprtGeometryOwner geometry_;
    HiprtFunctionTableOwner functions_;
};

}

int main(int argc, char** argv) {
    return run_demo<HiprtScene>(argc, argv, "hiprt_demo.bmp");
}
