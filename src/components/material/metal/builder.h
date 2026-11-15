#pragma once

#include "components/material/metal/description.h"
#include "components/material/metal/model.h"
#include "components/texture/builder.h"
#include "components/material/gpu_model.h"

namespace hip_demo::material::metal {
inline Model build(const Description& value, scene_ast::Position where, const SceneValues& values) {
    const Texture albedo = ConstantColor{values.color(value.albedo, where)};
    const float roughness = values.scalar(value.roughness, where);
    if (roughness < 0 || roughness > 1) values.errors.fail(where, "roughness must be in [0, 1]");
    return {albedo, roughness};
}

inline GpuMaterial prepare(const Model& value) {
    const auto texture = prepare_texture(value.albedo);
    return {texture.color, ShaderKind::metal, value.roughness, texture.kind};
}
}
