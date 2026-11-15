#pragma once

#include "scene/common.h"
#include <type_traits>

struct GpuMaterial {
    SceneVector albedo;
    ShaderKind kind = ShaderKind::diffuse;
    float roughness = 1;
    TextureKind texture = TextureKind::color;
    bool sampled_emitter = false;
};

static_assert(std::is_trivially_copyable_v<GpuMaterial>);
