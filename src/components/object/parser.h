#pragma once

#include "components/geometry/csg/parser.h"
#include "components/geometry/mesh/parser.h"
#include "components/geometry/rectangle/parser.h"
#include "components/object/description.h"

namespace hip_demo::parsing {
struct builtin_p_;
struct object_p_;

}
namespace hip_demo::object::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
using hip_demo::parsing::Sphere;
using hip_demo::parsing::Torus;
using hip_demo::parsing::Text;
using hip_demo::parsing::CsgNode;
using hip_demo::parsing::CsgBranch;
using hip_demo::parsing::Csg;
using hip_demo::parsing::Mesh;
using Parsed = std::variant<Sphere, Torus, Mesh, Text, Csg, SceneMeshKind, hip_demo::parsing::Rectangle>;
}
namespace hip_demo::parsing {
using Object = object::parsing::Parsed;
}
namespace hip_demo::object::parsing {
using hip_demo::parsing::Object;
inline bp::rule<hip_demo::parsing::builtin_p_, SceneMeshKind> builtin_p = "cat or crystal";
inline bp::rule<hip_demo::parsing::object_p_, Object> object_p = "object";

inline auto builtin_p_def = (keyword("cat") >> bp::attr(SceneMeshKind::cat)) |
                     (keyword("crystal") >> bp::attr(SceneMeshKind::crystal));
inline auto object_p_def = sphere_p | torus_p | mesh_p | text_p | csg_p | rectangle_p | builtin_p;
}
namespace hip_demo::parsing {
using object::parsing::builtin_p;
using object::parsing::builtin_p_def;
using object::parsing::object_p;
using object::parsing::object_p_def;
}
