#pragma once

#include "components/geometry/gpu_model.h"
#include "components/material/gpu_model.h"

struct GpuPrimitive {
    GpuGeometry geometry;
    GpuMaterial material;
};

static_assert(std::is_trivially_copyable_v<GpuPrimitive>);
