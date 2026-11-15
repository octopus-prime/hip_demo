#pragma once

#include "components/geometry/sphere/model.h"
#include "components/geometry/ellipsoid/model.h"
#include "components/geometry/cylinder/model.h"
#include "components/geometry/cone/model.h"
#include "components/geometry/paraboloid/model.h"
#include "components/geometry/hyperboloid/model.h"
#include "components/geometry/torus/model.h"
#include "components/geometry/text/model.h"
#include <variant>
#include <vector>

namespace hip_demo::geometry::csg {
struct Node {
    std::variant<Sphere, Torus, ExtrudedText, Ellipsoid, Cylinder, Cone, Paraboloid, Hyperboloid> geometry;
    CsgOperation operation = CsgOperation::unite;
    std::vector<Node> children;
};
struct Model { Node root; };
}
namespace hip_demo { using Csg = hip_demo::geometry::csg::Model; using CsgNode = hip_demo::geometry::csg::Node; }
