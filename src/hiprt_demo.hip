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
        candidate.object_id = result.primID;
        set_object_material(candidate, result.primID);
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
    unsigned int expected_object_id;
    unsigned int actual_object_id;
    float3 expected_normal;
    float3 actual_normal;
};

constexpr unsigned int special_ray_count = scene_object_count + 4;
constexpr unsigned int intersection_check_count = special_ray_count + 64 * 48;

__global__ void check_intersections(TraceScene scene, IntersectionCheck* results) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= intersection_check_count) {
        return;
    }
    Ray ray{};
    if (index < scene_object_count) {
        const SceneObject object = scene_object(index);
        if (object.sphere) {
            ray = {object.center + float3{0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
        } else {
            const float3 normal{0.0f, object.tilt_cosine, object.tilt_sine};
            ray = {object.center + float3{torus_major_radius, 0.0f, 0.0f} + 0.4f * normal, -normal};
        }
    } else if (index == scene_object_count) {
        ray = {{0.0f, 10.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    } else if (index == scene_object_count + 1) {
        ray = {scene_object(0).center, {1.0f, 0.0f, 0.0f}};
    } else if (index == scene_object_count + 2) {
        ray = {scene_object(1).center + float3{torus_major_radius, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    } else if (index == scene_object_count + 3) {
        ray = {scene_object(0).center - float3{2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    } else {
        const unsigned int pixel = index - special_ray_count;
        const float3 forward = normalize(camera_target - camera_position);
        const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const float3 up = cross(right, forward);
        const float u = (2.0f * (static_cast<float>(pixel % 64) + 0.5f) / 64 - 1.0f) *
            (static_cast<float>(image_width) / image_height) * 0.41421356f;
        const float v = (1.0f - 2.0f * (static_cast<float>(pixel / 64) + 0.5f) / 48) *
            0.41421356f;
        ray = {camera_position, normalize(forward + u * right + v * up)};
    }
    Hit expected{}, actual{};
    const bool expected_hit = hit_direct_objects(ray, expected);
    const bool actual_hit = hit_objects(ray, actual, scene);
    results[index] = {expected_hit, actual_hit, expected.distance, actual.distance,
                      expected.material, actual.material, expected.front_face, actual.front_face,
                      expected.object_id, actual.object_id, expected.normal, actual.normal};
}

__global__ void check_cornell_scene(TraceScene scene, unsigned int* failures) {
    unsigned int failed = 0;
    Hit hit{};
    const float3 origin{0.0f, 2.0f, 0.0f};
    if (!hit_room({origin, {0.0f, -1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::diffuse || fabsf(hit.distance - 2.0f) > 0.0001f ||
        hit_room({origin, {0.0f, -1.0f, 0.0f}}, 1.9f, hit)) {
        failed |= 1;
    }
    if (!hit_room({origin, {-1.0f, 0.0f, 0.0f}}, 1.0e30f, hit) || hit.albedo.x <= hit.albedo.y ||
        !hit_room({origin, {1.0f, 0.0f, 0.0f}}, 1.0e30f, hit) || hit.albedo.y <= hit.albedo.x ||
        !hit_room({origin, {0.0f, 0.0f, -1.0f}}, 1.0e30f, hit) || hit.material != Material::diffuse) {
        failed |= 2;
    }
    if (!hit_room({origin, {0.0f, 1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::emissive || !hit.front_face ||
        !hit_room({{2.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::diffuse ||
        !hit_room({{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 1.0e30f, hit) ||
        hit.material != Material::emissive || hit.front_face ||
        hit_room({origin, {0.0f, 0.0f, 1.0f}}, 1.0e30f, hit)) {
        failed |= 4;
    }
    if (!hit_scene({{-1.65f, 2.0f, -0.9f}, {0.0f, -1.0f, 0.0f}}, hit, scene) ||
        hit.object_id != 0 || hit.material != Material::metal) {
        failed |= 8;
    }
    Random random{123456789u};
    const float3 illuminated = trace({origin, {0.0f, 0.0f, -1.0f}}, random, scene);
    if (!(illuminated.x > 0.0f && illuminated.y > 0.0f && illuminated.z > 0.0f)) {
        failed |= 8;
    }
    float cosine_sum = 0.0f;
    for (int i = 0; i < 2048; ++i) {
        const float3 direction = sample_phong_lobe({0.0f, 1.0f, 0.0f}, random);
        if (!(direction.y >= 0.0f) || fabsf(dot(direction, direction) - 1.0f) > 0.0001f) {
            failed |= 16;
        }
        cosine_sum += direction.y;
    }
    const float expected_cosine = (phong_exponent + 1.0f) / (phong_exponent + 2.0f);
    if (fabsf(cosine_sum / 2048.0f - expected_cosine) > 0.005f) {
        failed |= 16;
    }
    *failures = failed;
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

        auto device_results = allocate_device<IntersectionCheck>(intersection_check_count, "Allocating intersection checks");
        hipLaunchKernelGGL(check_intersections, dim3((intersection_check_count + 127) / 128),
                           dim3(128), 0, 0, scene(), device_results.get());
        check_hip(hipGetLastError(), "Launching HIP RT intersection checks");
        std::array<IntersectionCheck, intersection_check_count> results{};
        check_hip(hipMemcpy(results.data(), device_results.get(), results.size() * sizeof(results[0]), hipMemcpyDeviceToHost),
                  "Reading HIP RT intersection checks");
        constexpr std::array<Material, scene_object_count> expected_materials{
            Material::metal, Material::glass, Material::phong,
            Material::phong, Material::glass, Material::diffuse};
        for (std::size_t index = 0; index < results.size(); ++index) {
            const IntersectionCheck& result = results[index];
            if ((index < scene_object_count && (!result.expected_hit || result.expected_object_id != index ||
                                               result.expected_material != expected_materials[index])) ||
                (index == scene_object_count && result.expected_hit) ||
                (index == scene_object_count + 1 &&
                 (!result.expected_hit || result.expected_object_id != 0 || result.expected_front_face)) ||
                (index == scene_object_count + 2 &&
                 (!result.expected_hit || result.expected_object_id != 1 || result.expected_front_face)) ||
                (index == scene_object_count + 3 && (!result.expected_hit || result.expected_object_id != 0))) {
                throw std::runtime_error("Direct intersection check failed for ray " + std::to_string(index));
            }
            if (index < scene_object_count && !scene_object(static_cast<unsigned int>(index)).sphere) {
                const SceneObject object = scene_object(static_cast<unsigned int>(index));
                const float3 normal{0.0f, object.tilt_cosine, object.tilt_sine};
                if (std::abs(result.expected_distance - (0.4f - torus_minor_radius)) > 0.003f ||
                    dot(result.expected_normal, normal) < 0.999f) {
                    throw std::runtime_error("Rotated torus intersection check failed for ray " + std::to_string(index));
                }
            }
            const float distance_tolerance = result.expected_hit &&
                !scene_object(result.expected_object_id).sphere ? 0.002f : 0.0001f;
            const float expected_normal_squared = dot(result.expected_normal, result.expected_normal);
            const float actual_normal_squared = dot(result.actual_normal, result.actual_normal);
            const float normal_dot = dot(result.expected_normal, result.actual_normal);
            if (result.expected_hit != result.actual_hit ||
                (result.expected_hit && (!std::isfinite(result.actual_distance) ||
                 std::abs(result.expected_distance - result.actual_distance) > distance_tolerance ||
                 result.expected_material != result.actual_material ||
                 result.expected_object_id != result.actual_object_id ||
                 result.expected_front_face != result.actual_front_face ||
                 !(expected_normal_squared > 0.0f) || !(actual_normal_squared > 0.0f) ||
                 !std::isfinite(actual_normal_squared) || !std::isfinite(normal_dot) ||
                 !(normal_dot >= 0.999f * std::sqrt(expected_normal_squared * actual_normal_squared))))) {
                throw std::runtime_error("HIP RT intersection check failed for ray " + std::to_string(index) +
                                         " (expected hit " + std::to_string(result.expected_hit) +
                                         " at " + std::to_string(result.expected_distance) +
                                         ", object " + std::to_string(result.expected_object_id) +
                                         ", got hit " + std::to_string(result.actual_hit) +
                                         " at " + std::to_string(result.actual_distance) +
                                         ", object " + std::to_string(result.actual_object_id) +
                                         ", normal dot " + std::to_string(normal_dot) +
                                         ", front faces " + std::to_string(result.expected_front_face) +
                                         "/" + std::to_string(result.actual_front_face) + ")");
            }
        }
        std::cout << "HIP RT custom primitive intersection checks passed.\n";
        auto device_failures = allocate_device<unsigned int>(1, "Allocating Cornell scene checks");
        hipLaunchKernelGGL(check_cornell_scene, dim3(1), dim3(1), 0, 0, scene(), device_failures.get());
        check_hip(hipGetLastError(), "Launching Cornell scene checks");
        unsigned int failures = 0;
        check_hip(hipMemcpy(&failures, device_failures.get(), sizeof(failures), hipMemcpyDeviceToHost),
                  "Reading Cornell scene checks");
        if (failures != 0) {
            throw std::runtime_error("Cornell scene checks failed (flags " + std::to_string(failures) + ")");
        }
        std::cout << "Cornell room, lighting and Phong checks passed.\n";
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
