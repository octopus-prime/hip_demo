#include "components/mesh/loading.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check_mesh(const std::filesystem::path& path) {
    const PlyMesh mesh = load_ply_mesh(path.string());
    const std::array<std::array<float, 3>, 3> expected_vertices{{
        {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}};
    if (mesh.vertices.size() != expected_vertices.size() || mesh.triangles.size() != 1 ||
        !std::equal(mesh.vertices.begin(), mesh.vertices.end(), expected_vertices.begin()) ||
        mesh.triangles[0] != std::array<std::uint32_t, 3>{0, 1, 2}) {
        throw std::runtime_error("Incorrect mesh loaded from " + path.string());
    }
}

void check_rejected(const std::filesystem::path& path, const std::string& reason) {
    try {
        (void)load_ply_mesh(path.string());
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message.find(path.string()) == std::string::npos || message.find(reason) == std::string::npos) {
            throw std::runtime_error("Unexpected error for " + path.string() + ": " + message);
        }
        return;
    }
    throw std::runtime_error("Invalid PLY accepted: " + path.string());
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected fixture directory");
        }
        const std::filesystem::path dir{argv[1]};
        check_mesh(dir / "triangle_ascii.ply");
        check_mesh(dir / "triangle_little.ply");
        check_mesh(dir / "triangle_big.ply");
        check_rejected(dir / "quad.ply", "exactly three");
        check_rejected(dir / "bad_index.ply", "index is invalid");
        check_rejected(dir / "bad_coordinate.ply", "not finite or representable");
        check_rejected(dir / "missing_coordinate.ply", "requires nonempty");
        check_rejected(dir / "truncated.ply", "Error reading");
        check_rejected(dir / "does_not_exist.ply", "Unable to open file");
        std::cout << "PLY loader checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PLY loader checks failed: " << error.what() << '\n';
        return 1;
    }
}
