#pragma once

#include "components/value/description.h"
#include "scene/common.h"
#include <string>

namespace hip_demo::geometry::mesh {

struct Description {
    scene_ast::Position where;
    std::string file;
    MeshUp up;
    scene_ast::Vector fit, position;
    std::string material;
};
}
namespace scene_ast { using Mesh = hip_demo::geometry::mesh::Description; }
