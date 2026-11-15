#pragma once

#include "components/value/parser.h"
#include "components/camera/description.h"

namespace hip_demo::parsing {
struct camera_p_;

}
namespace hip_demo::camera::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;
struct Parsed {
    Position where;
    Vector position, target;
    double half_view_height;
};
}
namespace hip_demo::parsing {
using Camera = camera::parsing::Parsed;
}
namespace hip_demo::camera::parsing {
using hip_demo::parsing::Camera;
inline bp::rule<hip_demo::parsing::camera_p_, Camera> camera_p = "camera";

inline auto camera_p_def = position_p >> bp::lit("camera") > '{'
    > bp::lit("position") > vector_p > bp::lit("target") > vector_p
    > keyword("half_view_height") > scalar_p > '}';
}
namespace hip_demo::parsing {
using camera::parsing::camera_p;
using camera::parsing::camera_p_def;
}
