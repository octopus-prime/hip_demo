#pragma once

#include "components/value/description.h"
#include "components/texture/description.h"
#include <optional>

namespace hip_demo::material::phong {
struct Description {
    scene_ast::Vector albedo;
    std::optional<scene_ast::TextureValue> texture;
};
}
