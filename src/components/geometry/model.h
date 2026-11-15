#pragma once

#include "components/geometry/csg/model.h"
#include "components/geometry/mesh/model.h"
#include "components/geometry/rectangle/model.h"
#include "components/geometry/ellipsoid/model.h"
#include "components/geometry/cylinder/model.h"
#include "components/geometry/cone/model.h"
#include "components/geometry/paraboloid/model.h"
#include "components/geometry/hyperboloid/model.h"

namespace hip_demo::geometry {
using Model = std::variant<Sphere, Torus, Mesh, ExtrudedText, Csg, Rectangle,
                           Ellipsoid, Cylinder, Cone, Paraboloid, Hyperboloid, Box>;
}
namespace hip_demo { using Geometry = geometry::Model; }
