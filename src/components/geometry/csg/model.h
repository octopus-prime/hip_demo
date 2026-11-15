#pragma once

#include "components/geometry/sphere/model.h"
#include "components/geometry/torus/model.h"
#include "components/geometry/text/model.h"
#include <variant>
#include <vector>

namespace hip_demo::geometry::csg {
struct Node {
    std::variant<Sphere, Torus, ExtrudedText> geometry;
    CsgOperation operation = CsgOperation::unite;
    std::vector<Node> children;
};
struct Model { Node root; };
}
namespace hip_demo { using Csg = hip_demo::geometry::csg::Model; using CsgNode = hip_demo::geometry::csg::Node; }
