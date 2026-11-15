#pragma once

#include "components/material/glass/description.h"
#include "components/material/glass/model.h"
#include "components/texture/builder.h"
#include "components/material/gpu_model.h"

namespace hip_demo::material::glass {
inline Model build(const Description& value, scene_ast::Position where, const SceneValues& values) {
    const Texture albedo = texture::build_albedo(value.albedo, value.texture, where, values);
    return {albedo};
}

inline GpuMaterial prepare(const Model& value) {
    const auto texture = prepare_texture(value.albedo);
    return {texture.color, ShaderKind::glass, 0, texture.kind};
}
}
