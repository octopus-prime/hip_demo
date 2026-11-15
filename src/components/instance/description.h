#pragma once

#include "components/object/description.h"
#include <optional>

namespace scene_ast {
struct Prototype {
    Position where;
    std::string name;
    Object object;
};
struct Instance {
    Position where;
    std::string prototype;
    std::optional<Vector> position, rotation, scale;
    std::optional<std::string> material;
};
}
