#pragma once

#include <hip/hip_runtime.h>
#include "scene/common.h"

#include <string>
#include <vector>

std::vector<float3> load_ply_vertices(const std::string& path, MeshUp up = MeshUp::z);
