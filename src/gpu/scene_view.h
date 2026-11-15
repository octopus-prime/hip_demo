#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>
#include <hiprt/hiprt_device.h>
#include "scene/types.h"
#include "components/scene/prepared.h"

namespace hip_demo {
struct GpuPrototype;

struct PrimitiveData {
    const GpuPrimitive* objects;
    unsigned int count;
    const TextGlyph* glyphs;
    const TextCurve* curves;
    unsigned int glyph_count;
    const GpuCsg* csgs;
    const GpuCsgLeaf* csg_leaves;
    unsigned int csg_count;
};

struct TraceScene {
    hiprtGeometry geometry;
    hiprtFuncTable functions;
    hiprtGeometry mesh;
    const float4* mesh_vertices;
    unsigned int mesh_triangle_count;
    unsigned int cat_detail_start;
    const GpuSceneSettings* settings;
    const GpuPrimitive* primitives;
    const TriangleMaterial* triangle_materials;
    const TextGlyph* glyphs;
    unsigned int primitive_count;
    const GpuCsg* csgs;
    unsigned int csg_count;
    const GpuEmitter* emitters = nullptr;
    unsigned int emitter_count = 0;
    const EmitterTriangle* emitter_triangles = nullptr;
    hiprtScene instance_scene = nullptr;
    const GpuInstance* instances = nullptr;
    const GpuPrototype* prototypes = nullptr;
};

struct GpuPrototype {
    TraceScene scene;
};
}
