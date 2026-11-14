#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>

#include "ply_mesh.h"
#include "scene_description.h"
#include "text_geometry.h"

namespace {
struct TriangleMaterial {
    SceneMaterial material;
    unsigned int object_id;
};

struct PrimitiveData {
    const ScenePrimitive* objects;
    unsigned int count;
    const TextGlyph* glyphs;
    const TextCurve* curves;
    unsigned int glyph_count;
};

struct TraceScene {
    hiprtGeometry geometry;
    hiprtFuncTable functions;
    hiprtGeometry mesh;
    const float4* mesh_vertices;
    unsigned int mesh_triangle_count;
    unsigned int cat_detail_start;
    bool detail;
    const SceneSettings* settings;
    const ScenePrimitive* primitives;
    const TriangleMaterial* triangle_materials;
    const TextGlyph* glyphs;
    unsigned int primitive_count;
};
}

#include "renderer_common.h"

// The linked HIP RT device library calls these global dispatch functions.
__device__ bool intersectFunc(std::uint32_t geom_type, std::uint32_t ray_type, const hiprtFuncTableHeader& table,
                              const hiprtRay& ray, void*, hiprtHit& result) {
    const auto* data = static_cast<const PrimitiveData*>(
        table.funcDataSets[geom_type * table.numRayTypes + ray_type].intersectFuncData);
    const Ray scene_ray{ray.origin, ray.direction};
    if (result.primID >= data->count) {
        const auto index = result.primID - data->count;
        TextIntersection hit{};
        if (index >= data->glyph_count ||
            !intersect_text(data->glyphs[index], data->curves,
                            {ray.origin.x, ray.origin.y, ray.origin.z},
                            {ray.direction.x, ray.direction.y, ray.direction.z},
                            ray.minT, ray.maxT, hit)) return false;
        result.t = hit.distance;
        result.normal = {hit.normal.x, hit.normal.y, hit.normal.z};
        return true;
    }
    Hit hit{};
    if (result.primID >= data->count ||
        !hit_described_primitive(scene_ray, data->objects[result.primID], ray.maxT, hit) ||
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
    if (!scene.geometry) return false;
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
        if (result.primID < scene.primitive_count) {
            candidate.object_id = result.primID;
            set_material(candidate, scene.primitives[result.primID].material);
        } else {
            const auto& glyph = scene.glyphs[result.primID - scene.primitive_count];
            candidate.object_id = glyph.object_id;
            set_material(candidate, glyph.material);
        }
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_custom_objects(ray, closest_hit, scene);
    if (!scene.mesh) return found;
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
        const auto& triangle = scene.triangle_materials[result.primID];
        closest_hit.object_id = triangle.object_id;
        set_material(closest_hit, triangle.material);
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

void append_ply_mesh(std::vector<float4>& vertices, const std::string& path, MeshUp mesh_up,
                     SceneVector fit, SceneVector position) {
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
    const std::array<double, 3> available{fit.x, fit.y, fit.z};
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
                static_cast<float>(position.y + (point[1] - minimum[1]) * scale),
                static_cast<float>(position.z + (point[2] - (minimum[2] + maximum[2]) * 0.5) * scale),
                0.0f});
        }
    }
    std::cout << "Loaded PLY mesh: " << path << " (" << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles).\n";
}

void append_ply_mesh(std::vector<float4>& vertices, const std::string& path, MeshUp mesh_up, bool detail = false) {
    append_ply_mesh(vertices, path, mesh_up,
                    detail ? SceneVector{1.85f, 2.45f, 1.55f} : SceneVector{1.05f, 1.55f, 0.9f},
                    detail ? SceneVector{0, 0.02f, -0.65f} : SceneVector{1.85f, 0.02f, 1.75f});
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

SceneDescription preset_description(const std::string& path, MeshUp up, bool detail) {
    auto description = load_scene_description(builtin_scene_path(detail));
    if (!path.empty()) override_scene_mesh(description, path, up);
    return description;
}

class HiprtScene {
public:
    explicit HiprtScene(int device, const std::string& ply_path = {}, MeshUp mesh_up = MeshUp::y,
                        bool detail = false)
        : HiprtScene(device, preset_description(ply_path, mesh_up, detail)) {}

    HiprtScene(int device, const SceneDescription& description)
        : detail_(description.settings.light.torus) {
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

        settings_ = allocate_device<SceneSettings>(1, "Allocating scene settings");
        check_hip(hipMemcpy(settings_.get(), &description.settings, sizeof(SceneSettings), hipMemcpyHostToDevice),
                  "Uploading scene settings");

        constexpr float margin = 0.01f;
        const auto object_count = static_cast<unsigned int>(description.primitives.size());
        primitive_count_ = object_count;
        TextGeometry text;
        for (std::size_t index = 0; index < description.texts.size(); ++index) {
            append_text_geometry(text, description.texts[index], object_count + static_cast<unsigned int>(index));
        }
        if (text.glyphs.size() > std::numeric_limits<unsigned int>::max() - object_count) {
            throw std::runtime_error("Scene exceeds the custom primitive index limit");
        }
        const auto custom_count = object_count + static_cast<unsigned int>(text.glyphs.size());
        std::vector<float3> bounds(static_cast<std::size_t>(custom_count) * 2);
        for (unsigned int index = 0; index < object_count; ++index) {
            const auto& object = description.primitives[index];
            const float sphere_size = object.radius;
            const float major = object.major_radius;
            const float minor = object.minor_radius;
            const float width = (object.sphere ? sphere_size : major + minor) + margin;
            const float absolute_sine = std::abs(object.tilt_sine);
            const float height = (object.sphere ? sphere_size : minor + major * absolute_sine) + margin;
            const float depth = (object.sphere ? sphere_size : minor + major * std::abs(object.tilt_cosine)) + margin;
            bounds[index * 2] = {object.center.x - width, object.center.y - height, object.center.z - depth};
            bounds[index * 2 + 1] = {object.center.x + width, object.center.y + height, object.center.z + depth};
        }
        if (object_count != 0) {
            primitives_ = allocate_device<ScenePrimitive>(object_count, "Allocating scene primitives");
            check_hip(hipMemcpy(primitives_.get(), description.primitives.data(),
                                object_count * sizeof(ScenePrimitive), hipMemcpyHostToDevice),
                      "Uploading scene primitives");
        }
        if (!text.glyphs.empty()) {
            text_glyphs_ = allocate_device<TextGlyph>(text.glyphs.size(), "Allocating text glyphs");
            text_curves_ = allocate_device<TextCurve>(text.curves.size(), "Allocating text curves");
            check_hip(hipMemcpy(text_glyphs_.get(), text.glyphs.data(), text.glyphs.size() * sizeof(TextGlyph),
                                hipMemcpyHostToDevice), "Uploading text glyphs");
            check_hip(hipMemcpy(text_curves_.get(), text.curves.data(), text.curves.size() * sizeof(TextCurve),
                                hipMemcpyHostToDevice), "Uploading text curves");
            for (std::size_t index = 0; index < text.glyphs.size(); ++index) {
                const auto& glyph = text.glyphs[index];
                const std::size_t offset = (object_count + index) * 2;
                bounds[offset] = {glyph.minimum.x - margin, glyph.minimum.y - margin, glyph.minimum.z - margin};
                bounds[offset + 1] = {glyph.maximum.x + margin, glyph.maximum.y + margin, glyph.maximum.z + margin};
            }
        }
        if (custom_count != 0) {
            bounds_ = allocate_device<float3>(bounds.size(), "Allocating HIP RT bounding boxes");
            check_hip(hipMemcpy(bounds_.get(), bounds.data(), bounds.size() * sizeof(float3),
                                hipMemcpyHostToDevice),
                      "Uploading HIP RT bounding boxes");

            hiprtGeometryBuildInput input{};
            input.type = hiprtPrimitiveTypeAABBList;
            input.geomType = 0;
            input.primitive.aabbList.aabbs = bounds_.get();
            input.primitive.aabbList.aabbCount = custom_count;
            input.primitive.aabbList.aabbStride = 2 * sizeof(float3);
            const hiprtBuildOptions options{};
            std::size_t temporary_size = 0;
            check_hiprt(
                hiprtGetGeometryBuildTemporaryBufferSize(context_.value, input, options, temporary_size),
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
            primitive_data_ = allocate_device<PrimitiveData>(1, "Allocating primitive dispatch data");
            const PrimitiveData data{primitives_.get(), object_count, text_glyphs_.get(), text_curves_.get(),
                                     static_cast<unsigned int>(text.glyphs.size())};
            check_hip(hipMemcpy(primitive_data_.get(), &data, sizeof(data), hipMemcpyHostToDevice),
                      "Uploading primitive dispatch data");
            function_data.intersectFuncData = primitive_data_.get();
            check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, 0, 0, function_data),
                        "Initializing HIP RT function table");
        }

        std::vector<float4> vertices;
        std::vector<TriangleMaterial> triangle_materials;
        unsigned int mesh_id = object_count + static_cast<unsigned int>(description.texts.size());
        for (const auto& mesh : description.meshes) {
            SceneMaterial material = mesh.material;
            if (mesh.kind == SceneMeshKind::crystal) {
                for (const uint3 triangle : mesh_triangles) {
                    append_triangle(vertices, mesh_points[triangle.x], mesh_points[triangle.y],
                                    mesh_points[triangle.z]);
                }
                material = {{0.10f, 0.65f, 0.72f}, Material::phong, 0};
            } else if (mesh.kind == SceneMeshKind::cat) {
                append_ellipsoid(vertices, cat_point(0.0f, 0.57f, 0.0f), {0.34f, 0.56f, 0.28f});
                append_ellipsoid(vertices, cat_point(0.0f, 1.20f, 0.0f), {0.31f, 0.31f, 0.26f});
                append_ear(vertices, -1.0f);
                append_ear(vertices, 1.0f);
                append_tail(vertices);
                cat_detail_start_ = static_cast<unsigned int>(vertices.size() / 3);
                material = {{0.78f, 0.36f, 0.16f}, Material::diffuse, 1};
                triangle_materials.resize(cat_detail_start_, {material, mesh_id});
                append_ellipsoid(vertices, cat_point(-0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
                append_ellipsoid(vertices, cat_point(0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
                append_ellipsoid(vertices, cat_point(0.0f, 1.14f, 0.255f), {0.035f, 0.025f, 0.025f});
                material = {{0.025f, 0.018f, 0.012f}, Material::diffuse, 1};
            } else {
                append_ply_mesh(vertices, mesh.file, mesh.up, mesh.fit, mesh.position);
                cat_detail_start_ = static_cast<unsigned int>(vertices.size() / 3);
            }
            triangle_materials.resize(vertices.size() / 3, {material, mesh_id++});
        }
        mesh_triangle_count_ = static_cast<unsigned int>(vertices.size() / 3);
        if (vertices.empty()) {
            return;
        }
        triangle_materials_ =
            allocate_device<TriangleMaterial>(triangle_materials.size(), "Allocating triangle materials");
        check_hip(hipMemcpy(triangle_materials_.get(), triangle_materials.data(),
                            triangle_materials.size() * sizeof(TriangleMaterial), hipMemcpyHostToDevice),
                  "Uploading triangle materials");
        mesh_vertices_ = allocate_device<float4>(vertices.size(), "Allocating mesh vertices");
        check_hip(hipMemcpy(mesh_vertices_.get(), vertices.data(), vertices.size() * sizeof(float4),
                            hipMemcpyHostToDevice),
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
                mesh_triangle_count_, cat_detail_start_, detail_, settings_.get(),
                primitives_.get(), triangle_materials_.get(), text_glyphs_.get(), primitive_count_};
    }

private:
    HiprtContextOwner context_;
    std::unique_ptr<SceneSettings, HipBufferDeleter> settings_;
    std::unique_ptr<ScenePrimitive, HipBufferDeleter> primitives_;
    std::unique_ptr<TextGlyph, HipBufferDeleter> text_glyphs_;
    std::unique_ptr<TextCurve, HipBufferDeleter> text_curves_;
    std::unique_ptr<float3, HipBufferDeleter> bounds_;
    HiprtGeometryOwner geometry_;
    std::unique_ptr<PrimitiveData, HipBufferDeleter> primitive_data_;
    HiprtFunctionTableOwner functions_;
    std::unique_ptr<float4, HipBufferDeleter> mesh_vertices_;
    std::unique_ptr<TriangleMaterial, HipBufferDeleter> triangle_materials_;
    HiprtGeometryOwner mesh_;
    unsigned int mesh_triangle_count_ = 0;
    unsigned int primitive_count_ = 0;
    unsigned int cat_detail_start_ = 0;
    bool detail_ = false;
};

}
