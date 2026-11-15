#pragma once

#include "gpu/runtime.h"
#include "gpu/scene_view.h"
#include "components/scene/preparation.h"
#include "components/scene/builder.h"

namespace hip_demo {

inline SceneDescription preset_description(const std::string& path, MeshUp up, bool detail) {
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
        : HiprtScene(device, prepare_scene(description)) {}

    HiprtScene(int device, PreparedScene prepared, hiprtContext shared_context = nullptr,
               unsigned int geometry_type = 0) : context_(shared_context) {
        hipCtx_t hip_context = nullptr;
        // HIP RT 3.0 requires this handle; HIP 7.1 has no non-deprecated context getter.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        check_hip(hipCtxGetCurrent(&hip_context), "Reading the current HIP context");
#pragma clang diagnostic pop
        if (!hip_context) {
            throw std::runtime_error("No current HIP context is available for HIP RT.");
        }
        hiprtContextCreationInput context_input{};
        context_input.ctxt = hip_context;
        context_input.device = device;
        context_input.deviceType = hiprtDeviceAMD;
        if (!shared_context)
            check_hiprt(hiprtCreateContext(HIPRT_API_VERSION, context_input, context_.value),
                        "Creating the HIP RT context");

        settings_ = allocate_device<GpuSceneSettings>(1, "Allocating scene settings");
        check_hip(hipMemcpy(settings_.get(), &prepared.settings, sizeof(GpuSceneSettings), hipMemcpyHostToDevice),
                  "Uploading scene settings");

        const auto object_count = static_cast<unsigned int>(prepared.primitives.size());
        primitive_count_ = object_count;
        csg_count_ = static_cast<unsigned int>(prepared.csgs.size());
        const auto& text = prepared.text;
        const auto& bounds = prepared.bounds;
        const auto custom_count = object_count + csg_count_ + prepared.glyph_count;
        if (csg_count_ != 0) {
            csgs_ = allocate_device<GpuCsg>(prepared.csgs.size(), "Allocating CSG objects");
            csg_leaves_ = allocate_device<GpuCsgLeaf>(prepared.csg_leaves.size(), "Allocating CSG leaves");
            check_hip(hipMemcpy(csgs_.get(), prepared.csgs.data(), prepared.csgs.size() * sizeof(GpuCsg),
                               hipMemcpyHostToDevice), "Uploading CSG objects");
            check_hip(hipMemcpy(csg_leaves_.get(), prepared.csg_leaves.data(),
                               prepared.csg_leaves.size() * sizeof(GpuCsgLeaf), hipMemcpyHostToDevice),
                      "Uploading CSG leaves");
        }
        if (object_count != 0) {
            primitives_ = allocate_device<GpuPrimitive>(object_count, "Allocating scene primitives");
            check_hip(hipMemcpy(primitives_.get(), prepared.primitives.data(),
                                object_count * sizeof(GpuPrimitive), hipMemcpyHostToDevice),
                      "Uploading scene primitives");
        }
        if (!text.glyphs.empty()) {
            text_glyphs_ = allocate_device<TextGlyph>(text.glyphs.size(), "Allocating text glyphs");
            text_curves_ = allocate_device<TextCurve>(text.curves.size(), "Allocating text curves");
            check_hip(hipMemcpy(text_glyphs_.get(), text.glyphs.data(), text.glyphs.size() * sizeof(TextGlyph),
                                hipMemcpyHostToDevice), "Uploading text glyphs");
            check_hip(hipMemcpy(text_curves_.get(), text.curves.data(), text.curves.size() * sizeof(TextCurve),
                                hipMemcpyHostToDevice), "Uploading text curves");
        }
        if (custom_count != 0) {
            bounds_ = allocate_device<float3>(bounds.size(), "Allocating HIP RT bounding boxes");
            check_hip(hipMemcpy(bounds_.get(), bounds.data(), bounds.size() * sizeof(float3),
                                hipMemcpyHostToDevice),
                      "Uploading HIP RT bounding boxes");

            hiprtGeometryBuildInput input{};
            input.type = hiprtPrimitiveTypeAABBList;
            input.geomType = geometry_type;
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
            const auto types = static_cast<unsigned int>(prepared.prototypes.size()) + geometry_type + 1;
            check_hiprt(hiprtCreateFuncTable(context_.value, types, 1, functions_.value),
                        "Creating HIP RT function table");
            hiprtFuncDataSet function_data{};
            primitive_data_ = allocate_device<PrimitiveData>(1, "Allocating primitive dispatch data");
            const PrimitiveData data{primitives_.get(), object_count, text_glyphs_.get(), text_curves_.get(),
                                     prepared.glyph_count,
                                     csgs_.get(), csg_leaves_.get(), csg_count_};
            check_hip(hipMemcpy(primitive_data_.get(), &data, sizeof(data), hipMemcpyHostToDevice),
                      "Uploading primitive dispatch data");
            function_data.intersectFuncData = primitive_data_.get();
            check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, geometry_type, 0, function_data),
                        "Initializing HIP RT function table");
        }

        emitter_count_ = static_cast<unsigned int>(prepared.emitters.size());
        if (emitter_count_ != 0) {
            emitters_ = allocate_device<GpuEmitter>(prepared.emitters.size(), "Allocating emitters");
            check_hip(hipMemcpy(emitters_.get(), prepared.emitters.data(),
                               prepared.emitters.size() * sizeof(GpuEmitter), hipMemcpyHostToDevice),
                      "Uploading emitters");
        }
        if (!prepared.emitter_triangles.empty()) {
            emitter_triangles_ = allocate_device<EmitterTriangle>(prepared.emitter_triangles.size(),
                                                                  "Allocating emitter triangle distributions");
            check_hip(hipMemcpy(emitter_triangles_.get(), prepared.emitter_triangles.data(),
                               prepared.emitter_triangles.size() * sizeof(EmitterTriangle), hipMemcpyHostToDevice),
                      "Uploading emitter triangle distributions");
        }
        const auto& vertices = prepared.vertices;
        const auto& triangle_materials = prepared.triangle_materials;
        cat_detail_start_ = prepared.cat_detail_start;
        mesh_triangle_count_ = static_cast<unsigned int>(vertices.size() / 3);
        if (vertices.empty()) {
            build_instances(device, prepared);
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
        mesh_input.geomType = geometry_type;
        mesh_input.primitive.triangleMesh.vertices = mesh_vertices_.get();
        mesh_input.primitive.triangleMesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
        mesh_input.primitive.triangleMesh.vertexStride = sizeof(float4);
        mesh_input.primitive.triangleMesh.triangleCount = mesh_triangle_count_;
        hiprtBuildOptions mesh_options{};
        mesh_options.buildFlags = hiprtBuildFlagBitPreferHighQualityBuild | hiprtBuildFlagBitDisableTrianglePairing;
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
        build_instances(device, prepared);
    }

    TraceScene scene() const {
        return {geometry_.value, functions_.value, mesh_.value, mesh_vertices_.get(),
                mesh_triangle_count_, cat_detail_start_, settings_.get(),
                primitives_.get(), triangle_materials_.get(), text_glyphs_.get(), primitive_count_,
                csgs_.get(), csg_count_, emitters_.get(), emitter_count_, emitter_triangles_.get(),
                instance_scene_.value, instances_.get(), prototype_views_.get()};
    }

private:
    void build_instances(int device, PreparedScene& prepared) {
        if (prepared.instances.empty()) return;
        if (!functions_.value) {
            functions_.context = context_.value;
            check_hiprt(hiprtCreateFuncTable(context_.value, static_cast<unsigned int>(prepared.prototypes.size()) + 1,
                                            1, functions_.value), "Creating instance function table");
        }
        prototype_scenes_.resize(prepared.prototypes.size());
        std::vector<GpuPrototype> views(prepared.prototypes.size());
        std::vector<bool> used(prepared.prototypes.size());
        for (const auto& placement : prepared.instances) used[placement.prototype] = true;
        std::size_t shared_triangles = 0, placed_triangles = 0;
        for (std::size_t i = 0; i < prepared.prototypes.size(); ++i) {
            if (!used[i]) continue;
            const auto type = static_cast<unsigned int>(i) + 1;
            auto& prototype = prototype_scenes_[i];
            prototype = std::make_unique<HiprtScene>(device, std::move(prepared.prototypes[i]), context_.value, type);
            views[i] = {prototype->scene()};
            shared_triangles += prototype->mesh_triangle_count_;
            hiprtFuncDataSet data{};
            data.intersectFuncData = prototype->primitive_data_.get();
            check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, type, 0, data),
                        "Registering instance geometry callbacks");
        }
        std::vector<hiprtInstance> references;
        std::vector<hiprtFrameMatrix> frames;
        references.reserve(prepared.instances.size());
        frames.reserve(prepared.instances.size());
        for (const auto& placement : prepared.instances) {
            const auto& prototype = *prototype_scenes_[placement.prototype];
            hiprtInstance reference{};
            reference.type = hiprtInstanceTypeGeometry;
            reference.geometry = prototype.geometry_.value ? prototype.geometry_.value : prototype.mesh_.value;
            references.push_back(reference);
            hiprtFrameMatrix frame{};
            for (int row = 0; row < 3; ++row) {
                const auto value = placement.transform.rows[row];
                frame.matrix[row][0] = value.x;
                frame.matrix[row][1] = value.y;
                frame.matrix[row][2] = value.z;
            }
            frame.matrix[0][3] = placement.transform.position.x;
            frame.matrix[1][3] = placement.transform.position.y;
            frame.matrix[2][3] = placement.transform.position.z;
            frames.push_back(frame);
            placed_triangles += prototype.mesh_triangle_count_;
        }
        instances_ = allocate_device<GpuInstance>(prepared.instances.size(), "Allocating instance shading data");
        prototype_views_ = allocate_device<GpuPrototype>(views.size(), "Allocating shared geometry views");
        instance_references_ = allocate_device<hiprtInstance>(references.size(), "Allocating HIP RT instances");
        instance_frames_ = allocate_device<hiprtFrameMatrix>(frames.size(), "Allocating instance frames");
        check_hip(hipMemcpy(instances_.get(), prepared.instances.data(), prepared.instances.size() * sizeof(GpuInstance),
                            hipMemcpyHostToDevice), "Uploading instance shading data");
        check_hip(hipMemcpy(prototype_views_.get(), views.data(), views.size() * sizeof(GpuPrototype),
                            hipMemcpyHostToDevice), "Uploading shared geometry views");
        check_hip(hipMemcpy(instance_references_.get(), references.data(), references.size() * sizeof(hiprtInstance),
                            hipMemcpyHostToDevice), "Uploading HIP RT instances");
        check_hip(hipMemcpy(instance_frames_.get(), frames.data(), frames.size() * sizeof(hiprtFrameMatrix),
                            hipMemcpyHostToDevice), "Uploading instance frames");
        hiprtSceneBuildInput input{};
        input.instances = instance_references_.get();
        input.instanceFrames = instance_frames_.get();
        input.instanceCount = static_cast<unsigned int>(references.size());
        input.frameCount = input.instanceCount;
        input.frameType = hiprtFrameTypeMatrix;
        hiprtBuildOptions options{};
        options.buildFlags = hiprtBuildFlagBitPreferHighQualityBuild;
        std::size_t temporary_size = 0;
        check_hiprt(hiprtGetSceneBuildTemporaryBufferSize(context_.value, input, options, temporary_size),
                    "Querying instance BVH build memory");
        std::unique_ptr<std::uint8_t, HipBufferDeleter> temporary;
        if (temporary_size) temporary = allocate_device<std::uint8_t>(temporary_size, "Allocating instance BVH memory");
        instance_scene_.context = context_.value;
        check_hiprt(hiprtCreateScene(context_.value, input, options, instance_scene_.value), "Creating instance BVH");
        check_hiprt(hiprtBuildScene(context_.value, hiprtBuildOperationBuild, input, options, temporary.get(), nullptr,
                                   instance_scene_.value), "Building instance BVH");
        check_hip(hipDeviceSynchronize(), "Waiting for instance BVH");
        std::cout << "Instancing: " << references.size() << " placements, "
                  << std::count(used.begin(), used.end(), true) << " shared geometries; "
                  << shared_triangles << " stored triangles / " << placed_triangles << " placed triangles.\n";
        std::cout << "Shared triangle vertex buffers: "
                  << shared_triangles * (3.0 * sizeof(float4) / (1024 * 1024)) << " MiB; duplicated placements would use "
                  << placed_triangles * (3.0 * sizeof(float4) / (1024 * 1024)) << " MiB (excluding BVHs/materials).\n";
    }

    HiprtContextOwner context_;
    std::unique_ptr<GpuSceneSettings, HipBufferDeleter> settings_;
    std::unique_ptr<GpuPrimitive, HipBufferDeleter> primitives_;
    std::unique_ptr<GpuCsg, HipBufferDeleter> csgs_;
    std::unique_ptr<GpuCsgLeaf, HipBufferDeleter> csg_leaves_;
    std::unique_ptr<TextGlyph, HipBufferDeleter> text_glyphs_;
    std::unique_ptr<TextCurve, HipBufferDeleter> text_curves_;
    std::unique_ptr<float3, HipBufferDeleter> bounds_;
    HiprtGeometryOwner geometry_;
    std::unique_ptr<PrimitiveData, HipBufferDeleter> primitive_data_;
    HiprtFunctionTableOwner functions_;
    std::unique_ptr<float4, HipBufferDeleter> mesh_vertices_;
    std::unique_ptr<TriangleMaterial, HipBufferDeleter> triangle_materials_;
    std::unique_ptr<GpuEmitter, HipBufferDeleter> emitters_;
    std::unique_ptr<EmitterTriangle, HipBufferDeleter> emitter_triangles_;
    HiprtGeometryOwner mesh_;
    unsigned int mesh_triangle_count_ = 0;
    unsigned int primitive_count_ = 0;
    unsigned int csg_count_ = 0;
    unsigned int cat_detail_start_ = 0;
    unsigned int emitter_count_ = 0;
    std::vector<std::unique_ptr<HiprtScene>> prototype_scenes_;
    std::unique_ptr<GpuInstance, HipBufferDeleter> instances_;
    std::unique_ptr<GpuPrototype, HipBufferDeleter> prototype_views_;
    std::unique_ptr<hiprtInstance, HipBufferDeleter> instance_references_;
    std::unique_ptr<hiprtFrameMatrix, HipBufferDeleter> instance_frames_;
    HiprtSceneOwner instance_scene_;
};

}
