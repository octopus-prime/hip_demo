#include "scene_description.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string replace(std::string text, const std::string& before, const std::string& after) {
    const auto position = text.find(before);
    require(position != std::string::npos, "test replacement not found");
    text.replace(position, before.size(), after);
    return text;
}

void rejects(const std::string& text, const char* reason) {
    try {
        parse_scene_description(text, "invalid.hipscene");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message.find("invalid.hipscene:") != 0 || message.find(reason) == std::string::npos) {
            throw std::runtime_error("Expected error containing '" + std::string(reason) + "', got: " + message);
        }
        return;
    }
    throw std::runtime_error("invalid scene accepted");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected fixture path");
        const auto classic = load_scene_description(builtin_scene_path(false));
        const auto detail = load_scene_description(builtin_scene_path(true));
        require(classic.primitives.size() == 7 && classic.meshes.size() == 2 &&
                classic.meshes[0].kind == SceneMeshKind::crystal &&
                classic.meshes[1].kind == SceneMeshKind::cat, "classic scene conversion");
        require(std::abs(classic.primitives[1].tilt_sine + 0.8660254f) < 1e-6f,
                "tilt_x uses degrees");
        require(detail.primitives.size() == 3 && detail.primitives.back().material.kind == Material::emissive &&
                detail.settings.surfaces[0].kind == SurfaceKind::tiles &&
                detail.settings.surfaces[1].kind == SurfaceKind::wood &&
                detail.settings.surfaces[2].kind == SurfaceKind::marble &&
                detail.settings.sampling.adaptive, "detail scene conversion");
        const auto scene = load_scene_description(argv[1]);
        require(scene.width == 17 && scene.height == 9 && scene.settings.sampling.maximum == 3 &&
                !scene.settings.sampling.adaptive && scene.settings.sampling.batch == 2, "image and sampling");
        require(scene.meshes.size() == 2 && scene.meshes[1].material.kind == Material::metal &&
                scene.meshes[1].position.x == 2, "independent mesh transforms and materials");
        require(std::filesystem::path(scene.meshes[0].file).is_absolute() &&
                std::filesystem::exists(scene.meshes[0].file), "mesh paths resolve relative to scene");
        std::ifstream file(argv[1]);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto compact = parse_scene_description(replace(text, "image {", "image{"));
        require(compact.width == 17, "punctuation does not require whitespace");
        require(compact.settings.surfaces[1].kind == SurfaceKind::wood &&
                compact.settings.surfaces[2].kind == SurfaceKind::marble,
                "wood ceiling and marble back survive grammar and scene conversion");
        const auto legacy = parse_scene_description(replace(text, "back marble_detail", "back wood_detail"));
        require(legacy.settings.surfaces[2].kind == SurfaceKind::wood, "existing wood backs remain supported");
        rejects(replace(text, "ceiling wood_detail", "ceiling marble_detail"), "ceiling surface");
        rejects(replace(text, "back marble_detail", "back marble_detail_extra"), "back surface");
        const auto escaped = parse_scene_description(replace(text, "scene_test.bmp", R"(dir\\a\"b.bmp)"));
        require(escaped.output == "dir\\a\"b.bmp", "quoted string escapes");
        const std::string text_object = R"(
text { font "font.ttf" value "BO S" size 1.5 depth 0.2 position [-1, 0.2, 0] material blue }
)";
        const auto with_text = parse_scene_description(text + text_object);
        require(with_text.texts.size() == 1 && with_text.texts[0].value == "BO S" &&
                with_text.texts[0].size == 1.5f && with_text.texts[0].depth == 0.2f &&
                with_text.texts[0].position.x == -1 && with_text.texts[0].material.kind == Material::metal,
                "text grammar and conversion");
        const auto text_fixture = std::filesystem::absolute(argv[1]).parent_path() / "text.hipscene";
        const auto loaded_text = load_scene_description(text_fixture.string());
        require(loaded_text.texts[0].font ==
                    (text_fixture.parent_path() / "fonts" / "example.ttf").lexically_normal().string(),
                "font paths resolve relative to scene");
        rejects(text + replace(text_object, "depth 0.2", "depth 0"), "positive");
        rejects(text + replace(text_object, "size 1.5", "size -1"), "positive");
        rejects(text + replace(text_object, "material blue", "material missing"), "unknown material");
        rejects(text + replace(text_object, "\"BO S\"", "\"\""), "must not be empty");
        const auto adaptive = parse_scene_description(replace(text, "sampling fixed {\n    count 3\n    batch 2\n}",
            "sampling adaptive {\n    min 3\n    specular_min 7\n    max 11\n    batch 4\n"
            "    relative_error 0.08\n    brightness_floor 0.03\n}")).settings.sampling;
        require(adaptive.adaptive && adaptive.minimum == 3 && adaptive.specular_minimum == 7 &&
                adaptive.maximum == 11 && adaptive.batch == 4 &&
                adaptive.relative_error == 0.08f && adaptive.brightness_floor == 0.03f,
                "adaptive rule preserves every sampling attribute");
        rejects(replace(text, "hipscene 1", "hipscene 2"), "invalid.hipscene:1:10: expected ");
        rejects(replace(text, "size [17, 9]", "size [17.5, 9]"), "expected ','");
        rejects(replace(text, "size [17, 9]", "size [0, 9]"), "integer");
        rejects(replace(text, "size [17, 9]", "size [2147483647, 9]"), "limits");
        rejects(replace(text, "radius 0.3", "radius -0.3"), "positive");
        rejects(replace(text, "roughness 0.3", "roughness 1.3"), "roughness");
        rejects(replace(text, "material red\n}", "material missing\n}"), "unknown material");
        rejects(replace(text, "material blue =", "material red ="), "duplicate material");
        rejects(replace(text, "up y", "up x"), "mesh up");
        rejects(replace(text, "radiance [3, 4, 5]", "radiance [-1, 4, 5]"), "radiance");
        rejects(replace(text, "radius 0.3", "radius 1e300"), "finite float");
        rejects(replace(text, "radius 0.3", "radius .3"), "positive number");
        rejects(replace(text, "radius 0.3", "radius +0.3"), "positive number");
        rejects(replace(text, "radius 0.3", "radius nan"), "positive number");
        rejects(replace(text, "count 3", "count 0"), "integer");
        rejects(replace(text, "target [0, 2, 0]", "target [0, 2, 12]"), "camera direction");
        rejects(replace(text, "minor_radius 0.1", "minor_radius 0.7"), "minor_radius");
        rejects(text + "garbage", "expected ");
        rejects(text + "catcrystal", "expected ");
        rejects(text.substr(0, text.rfind('}')), "expected '}'");
        auto overridden = classic;
        override_scene_mesh(overridden, "replacement.ply", MeshUp::z);
        require(overridden.meshes[1].file == "replacement.ply" &&
                overridden.meshes[1].position.y == 0.02f &&
                overridden.meshes[1].kind == SceneMeshKind::ply, "legacy cat replacement");
        std::cout << "Scene description tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene description tests failed: " << error.what() << '\n';
        return 1;
    }
}
