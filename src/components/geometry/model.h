#pragma once

#include "components/csg/model.h"
#include "components/mesh/model.h"

namespace hip_demo::geometry {
using Model = std::variant<Sphere, Torus, Mesh, ExtrudedText, Csg>;
}
namespace hip_demo { using Geometry = geometry::Model; }
