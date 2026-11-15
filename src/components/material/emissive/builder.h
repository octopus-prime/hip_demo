#pragma once

#include "components/material/emissive/description.h"
#include "components/material/emissive/model.h"
#include "components/texture/builder.h"
#include "components/material/gpu_model.h"
#include <cmath>
#include <stdexcept>

namespace hip_demo::material::emissive {
inline Model build(const Description& value, scene_ast::Position where, const SceneValues& values) {
    return {ConstantColor{values.radiance(value.radiance, where)}};
}

inline GpuMaterial prepare(const Model& value) {
    const auto texture = prepare_texture(value.radiance);
    if (!std::isfinite(texture.color.x) || !std::isfinite(texture.color.y) || !std::isfinite(texture.color.z) ||
        texture.color.x < 0 || texture.color.y < 0 || texture.color.z < 0)
        throw std::runtime_error("Emission radiance must be finite and nonnegative");
    return {texture.color, ShaderKind::emissive, 0, texture.kind};
}
}
