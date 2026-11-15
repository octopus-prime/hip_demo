#pragma once
#include "scene/common.h"

namespace hip_demo::geometry::cone {
struct Model {
    SceneVector center;
    float radius = 1, height = 2;
};
}
namespace hip_demo { using Cone = geometry::cone::Model; }
