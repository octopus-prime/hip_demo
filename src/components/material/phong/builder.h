#pragma once

#include "components/material/phong/description.h"
#include "components/material/phong/model.h"
#include "components/texture/builder.h"
#include "components/material/gpu_model.h"

namespace hip_demo::material::phong {
inline Model build(const Description& value, scene_ast::Position where, const SceneValues& values) {
    const Texture albedo = ConstantColor{values.color(value.albedo, where)};
    return {albedo};
}

inline GpuMaterial prepare(const Model& value) {
    const auto texture = prepare_texture(value.albedo);
    return {texture.color, ShaderKind::phong, 0, texture.kind};
}
}
