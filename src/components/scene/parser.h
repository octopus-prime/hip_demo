#pragma once

#include "components/camera/parser.h"
#include "components/room/parser.h"
#include "components/material/parser.h"
#include "components/light/parser.h"
#include "components/object/parser.h"
#include "components/scene/description.h"

namespace hip_demo::parsing {
struct scene_p_;

}
namespace hip_demo::scene::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
using hip_demo::parsing::Camera;
using hip_demo::parsing::Room;
using hip_demo::parsing::NamedMaterial;
using hip_demo::parsing::RectangleLight;
using hip_demo::parsing::TorusLight;
using hip_demo::parsing::Light;
using hip_demo::parsing::Sphere;
using hip_demo::parsing::Torus;
using hip_demo::parsing::Text;
using hip_demo::parsing::CsgNode;
using hip_demo::parsing::CsgBranch;
using hip_demo::parsing::Csg;
using hip_demo::parsing::Mesh;
using hip_demo::parsing::Object;
struct Parsed {
    Camera camera;
    Room room;
    std::vector<NamedMaterial> materials;
    std::optional<Light> light;
    std::vector<Object> objects;
};
}
namespace hip_demo::parsing {
using ParsedScene = scene::parsing::Parsed;
}
namespace hip_demo::scene::parsing {
using hip_demo::parsing::ParsedScene;
inline bp::rule<hip_demo::parsing::scene_p_, ParsedScene> scene_p = "hipscene 1 scene";

inline auto scene_p_def = keyword("hipscene") > keyword("1")
    > camera_p > room_p > *material_p > -light_p > *object_p > bp::eoi;
}
namespace hip_demo::parsing {
using scene::parsing::scene_p;
using scene::parsing::scene_p_def;
}
