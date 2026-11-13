#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct PlyMesh {
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

PlyMesh load_ply_mesh(const std::string& path);
