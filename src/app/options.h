#pragma once

#include "rendering/options.h"

namespace hip_demo {

struct AppOptions {
    std::string scene = "classic";
    RenderOptions render;
    std::string config_path, save_config_path;
    bool help = false;
    std::string help_text;
};

AppOptions load_options(int argc, char** argv);
void save_render_config(const RenderOptions& options, const std::string& path);

}
