#pragma once

#include "scene/ast.h"
#include "scene/types.h"

#include <string>

SceneDescription build_scene_description(const scene_ast::Scene& scene);
SceneDescription load_scene_description(const std::string& path);
std::string builtin_scene_path(bool detail);
void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up);
