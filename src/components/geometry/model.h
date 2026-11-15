#pragma once

#include "components/geometry/csg/model.h"
#include "components/geometry/mesh/model.h"
#include "components/geometry/rectangle/model.h"

namespace hip_demo::geometry {
using Model = std::variant<Sphere, Torus, Mesh, ExtrudedText, Csg, Rectangle>;
}
namespace hip_demo { using Geometry = geometry::Model; }
