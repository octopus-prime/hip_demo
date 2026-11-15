#pragma once

#include "components/sphere/model.h"
#include "components/torus/model.h"
#include "components/text/model.h"
#include <variant>
#include <vector>

namespace hip_demo::csg {
struct Node {
    std::variant<Sphere, Torus, ExtrudedText> geometry;
    CsgOperation operation = CsgOperation::unite;
    std::vector<Node> children;
};
struct Model { Node root; };
}
namespace hip_demo { using Csg = csg::Model; using CsgNode = csg::Node; }
