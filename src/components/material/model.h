#pragma once

#include "components/material/diffuse/model.h"
#include "components/material/phong/model.h"
#include "components/material/metal/model.h"
#include "components/material/glass/model.h"
#include "components/material/emissive/model.h"
#include <variant>

namespace hip_demo::material {
using Model = std::variant<Diffuse, Phong, Metal, Glass, Emissive>;
}
namespace hip_demo { using Material = material::Model; }
