#pragma once

#include "components/diffuse/model.h"
#include "components/phong/model.h"
#include "components/metal/model.h"
#include "components/glass/model.h"
#include "components/emissive/model.h"

namespace hip_demo::shader {

template <typename Visitor>
__device__ inline auto visit(Visitor visitor, ShaderKind kind) -> decltype(visitor(diffuse::Shader{})) {
    switch (kind) {
    case ShaderKind::diffuse: return visitor(diffuse::Shader{});
    case ShaderKind::phong: return visitor(phong::Shader{});
    case ShaderKind::metal: return visitor(metal::Shader{});
    case ShaderKind::glass: return visitor(glass::Shader{});
    case ShaderKind::emissive: return visitor(emissive::Shader{});
    }
    __builtin_trap();
}

}
