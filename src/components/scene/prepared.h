#pragma once

#include "components/geometry/text/outline.h"
#include "components/light/gpu_model.h"
#include "components/instance/gpu_model.h"
#include <hip/hip_runtime.h>
#include <vector>

namespace hip_demo {

struct TriangleMaterial {
    GpuMaterial material;
    unsigned int object_id;
};

struct PreparedScene {
    GpuSceneSettings settings;
    std::vector<GpuPrimitive> primitives;
    TextGeometry text;
    std::vector<float3> bounds;
    std::vector<float4> vertices;
    std::vector<TriangleMaterial> triangle_materials;
    unsigned int cat_detail_start = 0;
    std::vector<GpuCsg> csgs;
    std::vector<GpuCsgLeaf> csg_leaves;
    unsigned int glyph_count = 0;
    std::vector<GpuEmitter> emitters;
    std::vector<EmitterTriangle> emitter_triangles;
    std::vector<PreparedScene> prototypes;
    std::vector<GpuInstance> instances;
};

}
