#pragma once

#include "components/texture/color/parser.h"
#include "components/texture/wood/parser.h"
#include "components/texture/marble/parser.h"
#include "components/texture/tiles/parser.h"
#include "components/texture/mandelbrot/parser.h"
#include "components/texture/julia/parser.h"
#include "components/texture/description.h"

namespace hip_demo::parsing {
using scene_ast::TextureValue;
struct floor_p_;
struct ceiling_p_;
struct back_p_;
struct left_p_;
struct right_p_;

}
namespace hip_demo::texture::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::floor_p_, TextureValue> floor_p = "floor surface";
inline bp::rule<hip_demo::parsing::ceiling_p_, TextureValue> ceiling_p = "ceiling surface";
inline bp::rule<hip_demo::parsing::back_p_, TextureValue> back_p = "back surface";
inline bp::rule<hip_demo::parsing::left_p_, TextureValue> left_p = "left surface";
inline bp::rule<hip_demo::parsing::right_p_, TextureValue> right_p = "right surface";

inline auto floor_p_def = surface_p | tiles_p;
inline auto ceiling_p_def = surface_p | wood_p;
inline auto back_p_def = surface_p | wood_p | marble_p;
inline auto left_p_def = surface_p | mandelbrot_p;
inline auto right_p_def = surface_p | julia_p;
inline auto material_texture_p = surface_p | tiles_p | wood_p | mandelbrot_p | julia_p | marble_p;
}
namespace hip_demo::parsing {
using texture::parsing::floor_p;
using texture::parsing::floor_p_def;
using texture::parsing::ceiling_p;
using texture::parsing::ceiling_p_def;
using texture::parsing::back_p;
using texture::parsing::back_p_def;
using texture::parsing::left_p;
using texture::parsing::left_p_def;
using texture::parsing::right_p;
using texture::parsing::right_p_def;
using texture::parsing::material_texture_p;
}
