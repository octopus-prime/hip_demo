#include "app/options.h"
#include "saving/bmp.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

SceneDescription options(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    return hip_demo::load_options(static_cast<int>(argv.size()), argv.data());
}

void check_options(const std::string& fixture) {
    const auto original = options({"renderer", "--scene", fixture});
    const auto changed = options({"renderer", "changed.bmp", "9", "--fixed", "--scene", fixture});
    if (original.output != "scene_test.bmp" || original.settings.sampling.maximum != 3 ||
        changed.output != "changed.bmp" || changed.settings.sampling.maximum != 9 ||
        changed.settings.sampling.adaptive || changed.width != original.width ||
        changed.primitives.size() != original.primitives.size()) {
        throw std::runtime_error("CLI overrides did not preserve the scene and replace only supplied settings");
    }
    try {
        (void)options({"renderer", "--scene", fixture, "--mesh-up", "z"});
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("--mesh-up requires --mesh") != std::string::npos) return;
        throw;
    }
    throw std::runtime_error("--mesh-up without --mesh was accepted");
}

void check_bmp(const std::filesystem::path& output) {
    const hip_demo::Image image{1, 2, {0x332211u, 0x665544u}};
    hip_demo::save_bmp(image, output.string());
    std::ifstream input(output, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), {}};
    input.close();
    const std::array<unsigned char, 8> expected{0x33, 0x22, 0x11, 0, 0x66, 0x55, 0x44, 0};
    if (bytes.size() != 62 || bytes[0] != 'B' || bytes[1] != 'M' ||
        bytes[2] != bytes.size() || bytes[10] != 54 || bytes[18] != 1 ||
        bytes[22] != 0xfe || bytes[23] != 0xff || bytes[24] != 0xff || bytes[25] != 0xff ||
        !std::equal(expected.begin(), expected.end(), bytes.begin() + 54)) {
        throw std::runtime_error("BMP dimensions, top-down rows, BGR order, or padding are incorrect");
    }
    try {
        hip_demo::save_bmp(image, (output / "cannot_create.bmp").string());
        throw std::logic_error("An unwritable output path was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Could not open output image:") == std::string::npos) throw;
    }
    try {
        hip_demo::save_bmp({2, 2, {0}}, output.string());
        throw std::logic_error("An incomplete pixel buffer was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Invalid image dimensions or pixel count") == std::string::npos) throw;
    }
    std::filesystem::remove(output);
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Expected scene fixture and temporary BMP path");
        check_options(argv[1]);
        check_bmp(argv[2]);
        std::cout << "CLI and CPU-only BMP boundary checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "App support checks failed: " << error.what() << '\n';
        return 1;
    }
}
