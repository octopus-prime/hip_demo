#pragma once

#include "components/object/model.h"
#include "components/camera/model.h"
#include "components/room/model.h"
#include "components/instance/model.h"
#include <vector>

namespace hip_demo {
struct SceneSettings {
    camera::Model camera;
    room::Model room;
};
}
namespace hip_demo::scene {
struct Model {
    hip_demo::SceneSettings settings;
    std::vector<hip_demo::Object> objects;
    std::vector<hip_demo::Object> prototypes;
    std::vector<hip_demo::Instance> instances;
};
}
using SceneDescription = hip_demo::scene::Model;
