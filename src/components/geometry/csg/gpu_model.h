#pragma once

#include "components/object/gpu_model.h"

struct GpuCsgLeaf {
    GpuGeometry shape;
    std::uint32_t when_inside = csg_inside, when_outside = csg_outside;
    std::uint32_t text_index = 0, first_glyph = 0, glyph_count = 0;
    bool text = false;
};

struct GpuCsg {
    std::uint32_t first_leaf = 0, leaf_count = 0, entry = 0;
    GpuMaterial material;
};

static_assert(std::is_trivially_copyable_v<GpuCsgLeaf>);
