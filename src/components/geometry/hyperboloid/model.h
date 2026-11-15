#pragma once
#include "scene/common.h"

namespace hip_demo::geometry::hyperboloid {
struct Model {
    SceneVector center;
    float radius = 1, axial_radius = 1, height = 2;
};
}
namespace hip_demo { using Hyperboloid = geometry::hyperboloid::Model; }
