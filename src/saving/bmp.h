#pragma once

#include "saving/image.h"
#include <string>

namespace hip_demo {

void save_bmp(const Image& image, const std::string& output_path);

}
