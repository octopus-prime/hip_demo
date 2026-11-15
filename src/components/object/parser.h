#pragma once

#include "components/geometry/csg/parser.h"
#include "components/geometry/mesh/parser.h"
#include "components/geometry/rectangle/parser.h"
#include "components/geometry/ellipsoid/parser.h"
#include "components/geometry/cylinder/parser.h"
#include "components/geometry/cone/parser.h"
#include "components/geometry/paraboloid/parser.h"
#include "components/geometry/hyperboloid/parser.h"
#include "components/object/description.h"

namespace hip_demo::parsing {
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
using Parsed = std::variant<Sphere, Torus, Mesh, Text, Csg, SceneMeshKind, hip_demo::parsing::Rectangle,
    hip_demo::parsing::Ellipsoid, hip_demo::parsing::Cylinder, hip_demo::parsing::Cone,
    hip_demo::parsing::Paraboloid, hip_demo::parsing::Hyperboloid, hip_demo::parsing::Box>;
}
namespace hip_demo::parsing {
using Object = object::parsing::Parsed;
}
namespace hip_demo::object::parsing {
using hip_demo::parsing::Object;
inline bp::rule<hip_demo::parsing::object_p_, Object> object_p = "object";

inline auto builtin_p = (keyword("cat") >> bp::attr(SceneMeshKind::cat)) |
                     (keyword("crystal") >> bp::attr(SceneMeshKind::crystal));
inline auto object_p_def = sphere_p | torus_p | mesh_p | text_p | csg_p | rectangle_p |
    ellipsoid_p | cylinder_p | cone_p | paraboloid_p | hyperboloid_p | box_p | builtin_p;
}
namespace hip_demo::parsing {
using object::parsing::builtin_p;
using object::parsing::object_p;
using object::parsing::object_p_def;
}
