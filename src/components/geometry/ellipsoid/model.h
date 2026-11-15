#pragma once
#include "scene/common.h"

namespace hip_demo::geometry::ellipsoid {
struct Model {
    SceneVector center;
    SceneVector radii{1, 1, 1};
};
}
namespace hip_demo { using Ellipsoid = geometry::ellipsoid::Model; }
