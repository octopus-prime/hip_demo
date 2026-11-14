#pragma once

#include "surface/text.h"
#include <hip/hip_runtime.h>
#include <vector>

namespace hip_demo {

struct TriangleMaterial {
    SceneMaterial material;
    unsigned int object_id;
};

struct PreparedScene {
    SceneSettings settings;
    std::vector<ScenePrimitive> primitives;
    TextGeometry text;
    std::vector<float3> bounds;
    std::vector<float4> vertices;
    std::vector<TriangleMaterial> triangle_materials;
    unsigned int cat_detail_start = 0;
    std::vector<SceneCsg> csgs;
    std::vector<SceneCsgLeaf> csg_leaves;
};

}
