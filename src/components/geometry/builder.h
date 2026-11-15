#pragma once

#include "components/geometry/gpu_model.h"
#include "components/geometry/sphere/builder.h"
#include "components/geometry/ellipsoid/builder.h"
#include "components/geometry/cylinder/builder.h"
#include "components/geometry/cone/builder.h"
#include "components/geometry/paraboloid/builder.h"
#include "components/geometry/hyperboloid/builder.h"

namespace hip_demo {
inline GpuGeometry prepare_geometry(const Sphere& value) { return geometry::sphere::prepare(value); }
inline GpuGeometry prepare_geometry(const Ellipsoid& value) { return geometry::ellipsoid::prepare(value); }
inline GpuGeometry prepare_geometry(const Cylinder& value) { return geometry::cylinder::prepare(value); }
inline GpuGeometry prepare_geometry(const Cone& value) { return geometry::cone::prepare(value); }
inline GpuGeometry prepare_geometry(const Paraboloid& value) { return geometry::paraboloid::prepare(value); }
inline GpuGeometry prepare_geometry(const Hyperboloid& value) { return geometry::hyperboloid::prepare(value); }
inline GpuGeometry prepare_geometry(const Torus& value) { return value; }
}
