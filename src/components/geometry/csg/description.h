#pragma once

#include "components/value/description.h"
#include "components/geometry/sphere/description.h"
#include "components/geometry/ellipsoid/description.h"
#include "components/geometry/cylinder/description.h"
#include "components/geometry/cone/description.h"
#include "components/geometry/paraboloid/description.h"
#include "components/geometry/hyperboloid/description.h"
#include "components/geometry/torus/description.h"
#include "components/geometry/text/description.h"
#include "components/geometry/box/description.h"
#include "scene/common.h"
#include <variant>
#include <vector>

namespace hip_demo::geometry::csg {
struct NodeDescription {
    scene_ast::Position where;
    std::variant<scene_ast::Sphere, scene_ast::Torus, scene_ast::Text, scene_ast::Ellipsoid,
                 scene_ast::Cylinder, scene_ast::Cone, scene_ast::Paraboloid, scene_ast::Hyperboloid, scene_ast::Box> shape;
    CsgOperation operation = CsgOperation::unite;
    std::vector<NodeDescription> children;
};
struct Description {
    scene_ast::Position where;
    std::string material;
    NodeDescription root;
};
}
namespace scene_ast { using Csg = hip_demo::geometry::csg::Description; }

namespace scene_ast { using CsgNode = hip_demo::geometry::csg::NodeDescription; }
