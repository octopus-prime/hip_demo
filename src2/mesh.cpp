#include "mesh.h"
#include "components/geometry/mesh/fitting.h"

#include <algorithm>

std::vector<float3> load_ply_vertices(const std::string& path, MeshUp up)
{
    std::vector<float4> fitted;
    hip_demo::append_ply_mesh(fitted, path, up, {2, 2, 2}, {0, 0, 0});
    float height = 0;
    for (const auto& vertex : fitted) {
        height = std::max(height, vertex.y);
    }
    std::vector<float3> vertices;
    vertices.reserve(fitted.size());
    for (const auto& vertex : fitted) {
        vertices.push_back({vertex.x, vertex.y - height * 0.5f, vertex.z});
    }
    return vertices;
}
