#pragma once

#include <string>

namespace hip_demo {

struct SamplingOptions {
    int minimum = 64;
    int specular_minimum = 256;
    int maximum = 1024;
    int batch = 8;
    float relative_error = 0.05f;
    float brightness_floor = 0.05f;
    bool adaptive = true;
};

struct RenderOptions {
    int width = 3840, height = 2160;
    std::string output = "render.png";
    SamplingOptions sampling;
    int device = -1;
    bool unified_traversal = false;
};

}
