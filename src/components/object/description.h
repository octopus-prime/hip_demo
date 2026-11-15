#pragma once

#include "components/geometry/csg/description.h"
#include "components/geometry/mesh/description.h"
#include "components/geometry/rectangle/description.h"

namespace hip_demo::object {
using Description = std::variant<scene_ast::Sphere, scene_ast::Torus, scene_ast::Mesh,
    scene_ast::Text, scene_ast::Csg, SceneMeshKind, scene_ast::Rectangle>;
}
namespace scene_ast { using Object = hip_demo::object::Description; }
