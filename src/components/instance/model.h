#pragma once

#include "components/material/model.h"
#include <cstdint>
#include <optional>

namespace hip_demo {
struct InstanceTransform {
    SceneVector position{};
    SceneVector rotation{};
    SceneVector scale{1, 1, 1};
};
struct Instance {
    std::uint32_t prototype = 0;
    InstanceTransform transform;
    std::optional<Material> material;
};
}
