#pragma once

#include "scene/common.h"
#include <string>

namespace hip_demo::mesh {

struct Model {
    SceneMeshKind kind = SceneMeshKind::ply;
    std::string file;
    MeshUp up = MeshUp::y;
    SceneVector fit;
    SceneVector position;
};
}
namespace hip_demo { using Mesh = mesh::Model; }
