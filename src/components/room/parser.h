#pragma once

#include "components/texture/parser.h"
#include "components/room/description.h"

namespace hip_demo::parsing {
struct room_p_;

}
namespace hip_demo::room::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector size;
    TextureValue floor, ceiling, back, left, right;
};
}
namespace hip_demo::parsing {
using Room = room::parsing::Parsed;
}
namespace hip_demo::room::parsing {
using hip_demo::parsing::Room;
inline bp::rule<hip_demo::parsing::room_p_, Room> room_p = "room";

inline auto room_p_def = position_p >> bp::lit("room") > '{' > bp::lit("size") > vector_p
    > keyword("floor") > floor_p > keyword("ceiling") > ceiling_p > keyword("back") > back_p
    > keyword("left") > left_p > keyword("right") > right_p > '}';
}
namespace hip_demo::parsing {
using room::parsing::room_p;
using room::parsing::room_p_def;
}
