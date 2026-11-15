#pragma once

#include "components/geometry/model.h"
#include "components/material/model.h"
#include <optional>
#include <vector>

namespace hip_demo::object {

struct Model {
    Geometry geometry;
    Material material;
    std::vector<std::optional<Material>> csg_materials;
};
}
namespace hip_demo { using Object = object::Model; }
