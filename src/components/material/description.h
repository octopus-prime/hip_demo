#pragma once

#include "components/material/diffuse/description.h"
#include "components/material/phong/description.h"
#include "components/material/metal/description.h"
#include "components/material/glass/description.h"
#include "components/material/emissive/description.h"
#include <string>
#include <variant>

namespace scene_ast {
using MaterialValue = std::variant<hip_demo::material::diffuse::Description, hip_demo::material::phong::Description,
    hip_demo::material::metal::Description, hip_demo::material::glass::Description, hip_demo::material::emissive::Description>;
}
namespace hip_demo::material {
struct Description {
    scene_ast::Position where;
    std::string name;
    scene_ast::MaterialValue value;
};
}
namespace scene_ast { using NamedMaterial = hip_demo::material::Description; }
