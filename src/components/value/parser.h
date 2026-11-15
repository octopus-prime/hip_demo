#pragma once

#include <boost/parser/parser.hpp>
#include <string_view>
#include <string>
#include <utility>
#include "components/value/description.h"

namespace hip_demo::parsing {
namespace bp = boost::parser;
using Position = std::string_view::const_iterator;
using scene_ast::Vector;
inline const auto letter = bp::char_('a', 'z') | bp::char_('A', 'Z');
inline const auto identifier_char = letter | bp::digit | bp::char_('_');
inline const auto number_syntax = -bp::char_('-') >> +bp::digit >>
    -(bp::lit('.') >> +bp::digit) >> -(bp::char_("eE") >> -bp::char_("+-") >> +bp::digit);

inline auto keyword(const char* word) {
    return bp::lexeme[bp::lit(word) >> !identifier_char];
}
}
namespace hip_demo::parsing {
struct position_p_;
struct identifier_p_;
struct string_p_;
struct scalar_p_;
struct vector2_p_;
struct vector_p_;

}
namespace hip_demo::value::parsing {
using namespace hip_demo::parsing;
using hip_demo::parsing::Position;
using hip_demo::parsing::Vector;

inline bp::rule<hip_demo::parsing::position_p_, Position> position_p = "source position";
inline bp::rule<hip_demo::parsing::identifier_p_, std::string> identifier_p = "identifier";
inline bp::rule<hip_demo::parsing::string_p_, std::string> string_p = "quoted string";
inline bp::rule<hip_demo::parsing::scalar_p_, double> scalar_p = "number";
inline bp::rule<hip_demo::parsing::vector2_p_, std::array<double, 2>> vector2_p = "2D vector";
inline bp::rule<hip_demo::parsing::vector_p_, Vector> vector_p = "3D vector";

inline auto position_p_def = bp::eps[([](auto& context) { bp::_val(context) = bp::_where(context).begin(); })];
inline auto identifier_p_def = bp::lexeme[letter >> *identifier_char];
inline auto string_p_def = bp::lexeme['"' >>
    *((bp::lit('\\') >> bp::char_("\"\\")) | (bp::char_ - bp::char_("\"\\\r\n"))) > '"'];
inline auto scalar_p_def = bp::lexeme[&(number_syntax >> !(identifier_char | bp::char_("+.-"))) >> bp::double_];
inline auto vector2_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ']';
inline auto vector_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ',' > scalar_p > ']';
}
namespace hip_demo::parsing {
using value::parsing::position_p;
using value::parsing::position_p_def;
using value::parsing::identifier_p;
using value::parsing::identifier_p_def;
using value::parsing::string_p;
using value::parsing::string_p_def;
using value::parsing::scalar_p;
using value::parsing::scalar_p_def;
using value::parsing::vector2_p;
using value::parsing::vector2_p_def;
using value::parsing::vector_p;
using value::parsing::vector_p_def;
}
