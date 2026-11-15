#pragma once

#include "components/value/description.h"
#include "components/sphere/description.h"
#include "components/torus/description.h"
#include "components/text/description.h"
#include "scene/common.h"
#include <variant>
#include <vector>

namespace hip_demo::csg {
struct NodeDescription {
    scene_ast::Position where;
    std::variant<scene_ast::Sphere, scene_ast::Torus, scene_ast::Text> shape;
    CsgOperation operation = CsgOperation::unite;
    std::vector<NodeDescription> children;
};
struct Description {
    scene_ast::Position where;
    std::string material;
    NodeDescription root;
};
}
namespace scene_ast { using Csg = hip_demo::csg::Description; }

namespace scene_ast { using CsgNode = hip_demo::csg::NodeDescription; }
