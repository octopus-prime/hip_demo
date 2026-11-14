#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>
#include "scene/types.h"
#include "scene/geometry.h"

namespace hip_demo {

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
