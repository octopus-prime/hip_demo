#pragma once

#include "scene/ast.h"
#include "scene/types.h"

#include <string>

namespace hip_demo::scene {

Model build(const Description& scene);
Model load(const std::string& path);
std::string builtin_path(bool detail);
void override_mesh(Model& scene, const std::string& path, MeshUp up);

}

inline SceneDescription build_scene_description(const scene_ast::Scene& scene) {
    return hip_demo::scene::build(scene);
}

inline SceneDescription load_scene_description(const std::string& path) {
    return hip_demo::scene::load(path);
}

inline std::string builtin_scene_path(bool detail) {
    return hip_demo::scene::builtin_path(detail);
}

inline void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up) {
    hip_demo::scene::override_mesh(scene, path, up);
}
