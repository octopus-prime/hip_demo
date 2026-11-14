#pragma once

#include "scene/ast.h"
#include "scene/types.h"

#include <string>
#include <string_view>

// Owns the source text, source name, strings, and offset-based source positions.
scene_ast::Scene parse_scene_ast(std::string_view text, const std::string& source = "<scene>");

SceneDescription parse_scene_description(std::string_view text, const std::string& source = "<scene>");
