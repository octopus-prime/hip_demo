#pragma once

#include "components/texture/model.h"
#include <array>

namespace hip_demo::room {

struct Model {
    SceneVector size;
    std::array<Texture, 5> textures;
};

}
