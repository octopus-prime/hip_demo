#pragma once

#include <array>
#include <cstddef>

namespace scene_ast {
// Byte offsets into Scene::text remain valid when the AST is copied or moved.
using Position = std::size_t;
struct Vector {
    double x = 0, y = 0, z = 0;
};
}
