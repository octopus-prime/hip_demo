#pragma once

#include "components/material/diffuse/parser.h"
#include "components/material/phong/parser.h"
#include "components/material/metal/parser.h"
#include "components/material/glass/parser.h"
#include "components/material/emissive/parser.h"
#include "components/material/description.h"

namespace hip_demo::parsing {
using scene_ast::MaterialValue;
struct shader_p_;
struct material_p_;

}
namespace hip_demo::material::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    std::string name;
    MaterialValue value;
};
}
namespace hip_demo::parsing {
using NamedMaterial = material::parsing::Parsed;
}
namespace hip_demo::material::parsing {
using hip_demo::parsing::NamedMaterial;
inline bp::rule<hip_demo::parsing::shader_p_, MaterialValue> shader_p = "material kind";
inline bp::rule<hip_demo::parsing::material_p_, NamedMaterial> material_p = "named material";

inline auto shader_p_def = diffuse_p | phong_p | metal_p | glass_p | emissive_p;
inline auto material_p_def = position_p >> keyword("material") > identifier_p > '=' > shader_p;
}
namespace hip_demo::parsing {
using material::parsing::shader_p;
using material::parsing::shader_p_def;
using material::parsing::material_p;
using material::parsing::material_p_def;
}
