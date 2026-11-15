#pragma once

#include "components/diffuse/description.h"
#include "components/phong/description.h"
#include "components/metal/description.h"
#include "components/glass/description.h"
#include "components/emissive/description.h"
#include <string>
#include <variant>

namespace scene_ast {
using MaterialValue = std::variant<hip_demo::diffuse::Description, hip_demo::phong::Description,
    hip_demo::metal::Description, hip_demo::glass::Description, hip_demo::emissive::Description>;
}
namespace hip_demo::material {
struct Description {
    scene_ast::Position where;
    std::string name;
    scene_ast::MaterialValue value;
};
}
namespace scene_ast { using NamedMaterial = hip_demo::material::Description; }
