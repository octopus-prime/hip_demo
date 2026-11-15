#pragma once

#include "components/value/description.h"
#include "components/camera/description.h"
#include "components/room/description.h"
#include "components/material/description.h"
#include "components/light/description.h"
#include "components/object/description.h"
#include "components/instance/description.h"
#include <optional>

namespace hip_demo::scene {

struct Description {
    scene_ast::Camera camera;
    scene_ast::Room room;
    std::vector<scene_ast::NamedMaterial> materials;
    std::optional<scene_ast::Light> light;
    std::vector<scene_ast::Object> objects;
    std::string text;
    std::string source;
    std::vector<scene_ast::Prototype> prototypes;
    std::vector<scene_ast::Instance> instances;
};
}
namespace scene_ast { using Scene = hip_demo::scene::Description; }
