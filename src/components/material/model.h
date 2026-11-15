#pragma once

#include "components/diffuse/model.h"
#include "components/phong/model.h"
#include "components/metal/model.h"
#include "components/glass/model.h"
#include "components/emissive/model.h"
#include <variant>

namespace hip_demo::material {
using Model = std::variant<Diffuse, Phong, Metal, Glass, Emissive>;
}
namespace hip_demo { using Material = material::Model; }
