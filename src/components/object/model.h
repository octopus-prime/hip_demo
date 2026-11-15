#pragma once

#include "components/geometry/model.h"
#include "components/material/model.h"

namespace hip_demo::object {

struct Model {
    Geometry geometry;
    Material material;
};
}
namespace hip_demo { using Object = object::Model; }
