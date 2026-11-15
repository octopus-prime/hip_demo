#pragma once

#include "components/value/description.h"
#include "components/texture/description.h"
#include <optional>

namespace hip_demo::material::metal {
struct Description {
    scene_ast::Vector albedo;
    double roughness = 1;
    std::optional<scene_ast::TextureValue> texture;
};
}
