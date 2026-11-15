#pragma once

#include "components/object/parser.h"
#include "components/instance/description.h"

namespace hip_demo::instance::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Object;
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

inline auto prototype_p = bp::transform([](auto value) {
    return Prototype{bp::get(value, bp::llong<0>{}), std::move(bp::get(value, bp::llong<1>{})),
                     std::move(bp::get(value, bp::llong<2>{}))};
})[position_p >> keyword("prototype") > identifier_p > '=' > object_p];

inline auto instance_p = bp::transform([](auto value) {
    return Instance{bp::get(value, bp::llong<0>{}), std::move(bp::get(value, bp::llong<1>{})),
                    bp::get(value, bp::llong<2>{}), bp::get(value, bp::llong<3>{}),
                    bp::get(value, bp::llong<4>{}), std::move(bp::get(value, bp::llong<5>{}))};
})[position_p >> keyword("instance") > '{' > keyword("prototype") > identifier_p
    > -(keyword("position") > vector_p) > -(keyword("rotation") > vector_p)
    > -(keyword("scale") > vector_p) > -(keyword("material") > identifier_p) > '}'];
}
