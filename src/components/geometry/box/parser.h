#pragma once

#include "components/value/parser.h"
#include "components/geometry/box/description.h"

namespace hip_demo::geometry::box::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector center, size;
    std::string material;
};
inline auto make_box = [](auto value) {
    return Parsed{bp::get(value, bp::llong<0>{}), bp::get(value, bp::llong<1>{}),
                  bp::get(value, bp::llong<2>{}), std::move(bp::get(value, bp::llong<3>{}))};
};
inline auto box_fields_p = position_p >> keyword("box") > '{' > keyword("center") > vector_p
    > keyword("size") > vector_p;
inline auto box_p = bp::transform(make_box)[box_fields_p > keyword("material") > identifier_p > '}'];
inline auto csg_box_p = bp::transform(make_box)[box_fields_p > optional_material_reference_p > '}'];
}
namespace hip_demo::parsing {
using Box = geometry::box::parsing::Parsed;
using geometry::box::parsing::box_p;
using geometry::box::parsing::csg_box_p;
}
