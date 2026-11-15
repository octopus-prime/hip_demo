#pragma once

#include "components/geometry/sphere/parser.h"
#include "components/geometry/torus/parser.h"
#include "components/geometry/text/parser.h"
#include "components/geometry/csg/description.h"

namespace hip_demo::parsing {
struct csg_branch_p_;
struct csg_node_p_;
struct csg_p_;

}
namespace hip_demo::geometry::csg::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
using hip_demo::parsing::Sphere;
using hip_demo::parsing::Torus;
using hip_demo::parsing::Text;
struct NodeParsed {
    Position where;
    std::variant<Sphere, Torus, Text> shape;
    CsgOperation operation = CsgOperation::unite;
    std::vector<NodeParsed> children;
};
struct BranchParsed {
    Position where;
    CsgOperation operation;
    std::vector<NodeParsed> children;
};
struct Parsed {
    Position where;
    std::string material;
    NodeParsed root;
};
}
namespace hip_demo::parsing {
using CsgNode = hip_demo::geometry::csg::parsing::NodeParsed;
using CsgBranch = hip_demo::geometry::csg::parsing::BranchParsed;
using Csg = hip_demo::geometry::csg::parsing::Parsed;
}
namespace hip_demo::geometry::csg::parsing {
using hip_demo::parsing::CsgNode;
using hip_demo::parsing::CsgBranch;
using hip_demo::parsing::Csg;
inline bp::rule<hip_demo::parsing::csg_branch_p_, CsgBranch> csg_branch_p = "binary CSG operation";
inline bp::rule<hip_demo::parsing::csg_node_p_, CsgNode> csg_node_p = "CSG sphere, torus, text or operation";
inline bp::rule<hip_demo::parsing::csg_p_, Csg> csg_p = "CSG object";

inline auto csg_branch_p_def = position_p >> (
    (bp::lit("union") >> bp::attr(CsgOperation::unite)) |
    (bp::lit("intersection") >> bp::attr(CsgOperation::intersection)) |
    (bp::lit("difference") >> bp::attr(CsgOperation::difference)))
    > '{' > bp::repeat(2)[csg_node_p] > '}';
inline auto csg_node_p_def =
    csg_sphere_p[([](auto& context) {
        const auto& shape = bp::_attr(context);
        bp::_val(context) = CsgNode{shape.where, shape, CsgOperation::unite, {}};
    })] |
    csg_torus_p[([](auto& context) {
        const auto& shape = bp::_attr(context);
        bp::_val(context) = CsgNode{shape.where, shape, CsgOperation::unite, {}};
    })] |
    csg_text_p[([](auto& context) {
        const auto& shape = bp::_attr(context);
        bp::_val(context) = CsgNode{shape.where, shape, CsgOperation::unite, {}};
    })] |
    csg_branch_p[([](auto& context) {
        auto& branch = bp::_attr(context);
        bp::_val(context) = CsgNode{branch.where, Sphere{}, branch.operation, std::move(branch.children)};
    })];
inline auto csg_p_def = position_p >> bp::lit("csg") > '{'
    > keyword("material") > identifier_p > csg_node_p > '}';
}
namespace hip_demo::parsing {
using hip_demo::geometry::csg::parsing::csg_branch_p;
using hip_demo::geometry::csg::parsing::csg_branch_p_def;
using hip_demo::geometry::csg::parsing::csg_node_p;
using hip_demo::geometry::csg::parsing::csg_node_p_def;
using hip_demo::geometry::csg::parsing::csg_p;
using hip_demo::geometry::csg::parsing::csg_p_def;
}
