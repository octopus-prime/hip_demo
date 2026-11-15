#pragma once

#include "components/value/parser.h"
#include "components/geometry/mesh/description.h"

namespace hip_demo::parsing {
struct mesh_up_p_;
struct mesh_p_;

}
namespace hip_demo::geometry::mesh::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    std::string file;
    MeshUp up;
    Vector fit, position;
    std::string material;
};
}
namespace hip_demo::parsing {
using Mesh = hip_demo::geometry::mesh::parsing::Parsed;
}
namespace hip_demo::geometry::mesh::parsing {
using hip_demo::parsing::Mesh;
inline bp::rule<hip_demo::parsing::mesh_up_p_, MeshUp> mesh_up_p = "mesh up (y or z)";
inline bp::rule<hip_demo::parsing::mesh_p_, Mesh> mesh_p = "mesh";

inline auto mesh_up_p_def = (keyword("y") >> bp::attr(MeshUp::y)) | (keyword("z") >> bp::attr(MeshUp::z));
inline auto mesh_p_def = position_p >> bp::lit("mesh") > '{'
    > bp::lit("file") > string_p > keyword("up") > mesh_up_p > bp::lit("fit") > vector_p
    > bp::lit("position") > vector_p > keyword("material") > identifier_p > '}';
}
namespace hip_demo::parsing {
using hip_demo::geometry::mesh::parsing::mesh_up_p;
using hip_demo::geometry::mesh::parsing::mesh_up_p_def;
using hip_demo::geometry::mesh::parsing::mesh_p;
using hip_demo::geometry::mesh::parsing::mesh_p_def;
}
