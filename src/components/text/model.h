#pragma once

#include "scene/common.h"
#include <string>

namespace hip_demo::text {

struct Model {
    std::string font, value;
    float size = 1, depth = 0.2f;
    SceneVector position;
    bool operator==(const Model&) const = default;
};
}
namespace hip_demo { using ExtrudedText = text::Model; }
