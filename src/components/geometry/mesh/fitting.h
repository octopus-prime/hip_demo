#pragma once

#include "components/geometry/mesh/procedural.h"
#include "components/geometry/mesh/loading.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <string>

namespace hip_demo {

inline void append_ply_mesh(std::vector<float4>& vertices, const std::string& path, MeshUp mesh_up,
                     SceneVector fit, SceneVector position) {
    const PlyMesh mesh = load_ply_mesh(path);
    const auto orient = [mesh_up](const std::array<float, 3>& point) {
        return mesh_up == MeshUp::z
            ? std::array<double, 3>{point[0], point[2], -static_cast<double>(point[1])}
            : std::array<double, 3>{point[0], point[1], point[2]};
    };
    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double, 3> maximum{-minimum[0], -minimum[1], -minimum[2]};
    for (const auto& triangle : mesh.triangles) {
        for (const auto index : triangle) {
            const auto point = orient(mesh.vertices[index]);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
        }
    }
    const std::array<double, 3> available{fit.x, fit.y, fit.z};
    double scale = std::numeric_limits<double>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double extent = maximum[axis] - minimum[axis];
        if (extent > 0) {
            scale = std::min(scale, available[axis] / extent);
        }
    }
    if (!std::isfinite(scale)) {
        throw std::runtime_error("PLY '" + path + "': mesh has zero extent");
    }
    vertices.reserve(vertices.size() + mesh.triangles.size() * 3);
    for (const auto& triangle : mesh.triangles) {
        for (const auto index : triangle) {
            const auto point = orient(mesh.vertices[index]);
            vertices.push_back({
                static_cast<float>(position.x + (point[0] - (minimum[0] + maximum[0]) * 0.5) * scale),
                static_cast<float>(position.y + (point[1] - minimum[1]) * scale),
                static_cast<float>(position.z + (point[2] - (minimum[2] + maximum[2]) * 0.5) * scale),
                0.0f});
        }
    }
    std::cout << "Loaded PLY mesh: " << path << " (" << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles).\n";
}

inline void append_ply_mesh(std::vector<float4>& vertices, const std::string& path, MeshUp mesh_up, bool detail = false) {
    append_ply_mesh(vertices, path, mesh_up,
                    detail ? SceneVector{1.85f, 2.45f, 1.55f} : SceneVector{1.05f, 1.55f, 0.9f},
                    detail ? SceneVector{0, 0.02f, -0.65f} : SceneVector{1.85f, 0.02f, 1.75f});
}

}
