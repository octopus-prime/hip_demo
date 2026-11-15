#pragma once

#include "components/material/diffuse/model.h"
#include "components/material/phong/model.h"
#include "components/material/metal/model.h"
#include "components/material/glass/model.h"
#include "components/material/emissive/model.h"

namespace hip_demo::shader {

template <typename Visitor>
__device__ inline auto visit(Visitor visitor, ShaderKind kind) -> decltype(visitor(hip_demo::material::diffuse::Shader{})) {
    switch (kind) {
    case ShaderKind::diffuse: return visitor(hip_demo::material::diffuse::Shader{});
    case ShaderKind::phong: return visitor(hip_demo::material::phong::Shader{});
    case ShaderKind::metal: return visitor(hip_demo::material::metal::Shader{});
    case ShaderKind::glass: return visitor(hip_demo::material::glass::Shader{});
    case ShaderKind::emissive: return visitor(hip_demo::material::emissive::Shader{});
    }
    __builtin_trap();
}

}
