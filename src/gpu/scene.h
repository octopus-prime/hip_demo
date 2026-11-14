#pragma once

#include "gpu/runtime.h"
#include "gpu/scene_view.h"
#include "building/geometry.h"
#include "building/scene_description.h"

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

    HiprtScene(int device, PreparedScene prepared)
        : detail_(prepared.settings.light.torus) {
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
        check_hiprt(hiprtCreateContext(HIPRT_API_VERSION, context_input, context_.value),
                    "Creating the HIP RT context");

        settings_ = allocate_device<SceneSettings>(1, "Allocating scene settings");
        check_hip(hipMemcpy(settings_.get(), &prepared.settings, sizeof(SceneSettings), hipMemcpyHostToDevice),
                  "Uploading scene settings");

        const auto object_count = static_cast<unsigned int>(prepared.primitives.size());
        primitive_count_ = object_count;
        csg_count_ = static_cast<unsigned int>(prepared.csgs.size());
        const auto& text = prepared.text;
        const auto& bounds = prepared.bounds;
        const auto custom_count = object_count + csg_count_ + prepared.glyph_count;
        if (csg_count_ != 0) {
            csgs_ = allocate_device<SceneCsg>(prepared.csgs.size(), "Allocating CSG objects");
            csg_leaves_ = allocate_device<SceneCsgLeaf>(prepared.csg_leaves.size(), "Allocating CSG leaves");
            check_hip(hipMemcpy(csgs_.get(), prepared.csgs.data(), prepared.csgs.size() * sizeof(SceneCsg),
                               hipMemcpyHostToDevice), "Uploading CSG objects");
            check_hip(hipMemcpy(csg_leaves_.get(), prepared.csg_leaves.data(),
                               prepared.csg_leaves.size() * sizeof(SceneCsgLeaf), hipMemcpyHostToDevice),
                      "Uploading CSG leaves");
        }
        if (object_count != 0) {
            primitives_ = allocate_device<ScenePrimitive>(object_count, "Allocating scene primitives");
            check_hip(hipMemcpy(primitives_.get(), prepared.primitives.data(),
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
                                     prepared.glyph_count,
                                     csgs_.get(), csg_leaves_.get(), csg_count_};
            check_hip(hipMemcpy(primitive_data_.get(), &data, sizeof(data), hipMemcpyHostToDevice),
                      "Uploading primitive dispatch data");
            function_data.intersectFuncData = primitive_data_.get();
            check_hiprt(hiprtSetFuncTable(context_.value, functions_.value, 0, 0, function_data),
                        "Initializing HIP RT function table");
        }

        const auto& vertices = prepared.vertices;
        const auto& triangle_materials = prepared.triangle_materials;
        cat_detail_start_ = prepared.cat_detail_start;
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
                primitives_.get(), triangle_materials_.get(), text_glyphs_.get(), primitive_count_,
                csgs_.get(), csg_count_};
    }

private:
    HiprtContextOwner context_;
    std::unique_ptr<SceneSettings, HipBufferDeleter> settings_;
    std::unique_ptr<ScenePrimitive, HipBufferDeleter> primitives_;
    std::unique_ptr<SceneCsg, HipBufferDeleter> csgs_;
    std::unique_ptr<SceneCsgLeaf, HipBufferDeleter> csg_leaves_;
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
    unsigned int csg_count_ = 0;
    unsigned int cat_detail_start_ = 0;
    bool detail_ = false;
};

}
