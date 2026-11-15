#include "building/scene_description.h"
#include "parsing/parser.h"
#include "building/material.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

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

void expect_error(auto&& action, const char* reason) {
    try {
        action();
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message.find("invalid.hipscene:") != 0 || message.find(reason) == std::string::npos) {
            throw std::runtime_error("Expected error containing '" + std::string(reason) + "', got: " + message);
        }
        return;
    }
    throw std::runtime_error("invalid scene accepted");
}

void rejects(const std::string& text, const char* reason) {
    expect_error([&] { parse_scene_description(text, "invalid.hipscene"); }, reason);
}

scene_ast::Scene detached_ast(std::string text, std::string source) {
    scene_ast::Scene copied;
    {
        const auto original = parse_scene_ast(text, source);
        copied = original;
    }
    text.assign(text.size(), 'x');
    source.assign(source.size(), 'x');
    return scene_ast::Scene(std::move(copied));
}

void rejects_build(const std::string& text, const char* reason) {
    const auto ast = detached_ast(text, "invalid.hipscene");
    expect_error([&] { build_scene_description(ast); }, reason);
}

template <typename Geometry>
auto objects(const SceneDescription& scene) {
    std::vector<const hip_demo::Object*> result;
    for (const auto& object : scene.objects)
        if (std::holds_alternative<Geometry>(object.geometry)) result.push_back(&object);
    return result;
}

template <typename Geometry>
const Geometry& geometry(const SceneDescription& scene, std::size_t index = 0) {
    return std::get<Geometry>(objects<Geometry>(scene).at(index)->geometry);
}

template <typename Geometry, typename Material>
const Material& material(const SceneDescription& scene, std::size_t index = 0) {
    return std::get<Material>(objects<Geometry>(scene).at(index)->material);
}

std::size_t leaves(const hip_demo::CsgNode& node) {
    if (node.children.empty()) return 1;
    std::size_t result = 0;
    for (const auto& child : node.children) result += leaves(child);
    return result;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected fixture path");
        const auto classic = load_scene_description(builtin_scene_path(false));
        const auto detail = load_scene_description(builtin_scene_path(true));
        using namespace hip_demo;
        require(objects<Sphere>(classic).size() + objects<Torus>(classic).size() == 7 &&
                objects<Mesh>(classic).size() == 2 &&
                geometry<Mesh>(classic, 0).kind == SceneMeshKind::crystal &&
                geometry<Mesh>(classic, 1).kind == SceneMeshKind::cat, "classic scene conversion");
        require(std::abs(geometry<Torus>(classic).tilt_sine + 0.8660254f) < 1e-6f,
                "tilt_x uses degrees");
        require(objects<Sphere>(detail).size() + objects<Torus>(detail).size() == 3 &&
                std::holds_alternative<Emissive>(detail.objects.back().material) &&
                std::holds_alternative<Tiles>(detail.settings.textures[0]) &&
                std::holds_alternative<Wood>(detail.settings.textures[1]) &&
                std::holds_alternative<Marble>(detail.settings.textures[2]), "detail scene conversion");
        const auto scene = load_scene_description(argv[1]);
        require(objects<Mesh>(scene).size() == 2 && material<Mesh, Metal>(scene, 1).roughness == 0.3f &&
                geometry<Mesh>(scene, 1).position.x == 2, "independent mesh transforms and materials");
        require(std::filesystem::path(geometry<Mesh>(scene).file).is_absolute() &&
                std::filesystem::exists(geometry<Mesh>(scene).file), "mesh paths resolve relative to scene");
        std::ifstream file(argv[1]);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto compact = parse_scene_description(replace(text, "camera {", "camera{"));
        require(compact.settings.camera_position.z == 12, "punctuation does not require whitespace");
        require(std::holds_alternative<Wood>(compact.settings.textures[1]) &&
                std::holds_alternative<Marble>(compact.settings.textures[2]),
                "wood ceiling and marble back survive grammar and scene conversion");
        const auto legacy = parse_scene_description(replace(text, "back marble_detail", "back wood_detail"));
        require(std::holds_alternative<Wood>(legacy.settings.textures[2]), "existing wood backs remain supported");
        rejects(replace(text, "ceiling wood_detail", "ceiling marble_detail"), "ceiling surface");
        rejects(replace(text, "back marble_detail", "back marble_detail_extra"), "back surface");
        const std::string text_object = R"(
text { font "font.ttf" value "BO S" size 1.5 depth 0.2 position [-1, 0.2, 0] material blue }
)";
        const auto escaped = parse_scene_description(text + replace(text_object, "font.ttf", R"(dir\\a\"b.ttf)"));
        require(geometry<ExtrudedText>(escaped).font == "dir\\a\"b.ttf", "quoted string escapes");
        const auto with_text = parse_scene_description(text + text_object);
        require(objects<ExtrudedText>(with_text).size() == 1 && geometry<ExtrudedText>(with_text).value == "BO S" &&
                geometry<ExtrudedText>(with_text).size == 1.5f && geometry<ExtrudedText>(with_text).depth == 0.2f &&
                geometry<ExtrudedText>(with_text).position.x == -1 &&
                std::holds_alternative<Metal>(objects<ExtrudedText>(with_text)[0]->material),
                "text grammar and conversion");
        const auto owned_ast = detached_ast(text + text_object, "owned.hipscene");
        require(owned_ast.text == text + text_object && owned_ast.source == "owned.hipscene" &&
                owned_ast.text.substr(owned_ast.camera.where, 6) == "camera",
                "AST owns source text, name, and stable source offsets after copy and move");
        const auto owned_scene = build_scene_description(owned_ast);
        require(objects<Mesh>(owned_scene).size() == 2 &&
                geometry<Mesh>(owned_scene).file == "triangle_ascii.ply" &&
                std::holds_alternative<Metal>(objects<Mesh>(owned_scene)[1]->material) &&
                objects<ExtrudedText>(owned_scene).size() == 1 && geometry<ExtrudedText>(owned_scene).font == "font.ttf" &&
                geometry<ExtrudedText>(owned_scene).value == "BO S" &&
                std::holds_alternative<Metal>(objects<ExtrudedText>(owned_scene)[0]->material),
                "copied and moved AST builds after caller input and original AST are destroyed");
        const auto invalid_ast = detached_ast(
            "hipscene 1\n"
            "camera { position [0, 0, 1] target [0, 0, 0] half_view_height 1 }\n"
            "room { size [4, 4, 4] floor color [0, 0, 0] ceiling color [0, 0, 0]\n"
            "back color [0, 0, 0] left color [0, 0, 0] right color [0, 0, 0] }\n"
            "material red = diffuse [1, 0, 0]\n"
            "  material red = diffuse [0, 1, 0]\n"
            "light ceiling_rectangle { center [0, 0] size [1, 1] radiance [1, 1, 1] }\n",
            "semantic.hipscene");
        bool semantic_error = false;
        try {
            build_scene_description(invalid_ast);
        } catch (const std::runtime_error& error) {
            semantic_error = true;
            require(std::string(error.what()) ==
                        "semantic.hipscene:6:3: duplicate material 'red'",
                    "semantic diagnostics retain exact source, line, and column after AST copy and move");
        }
        require(semantic_error, "semantic validation belongs to the build phase");
        const std::string csg = R"(
csg {
    material blue
    difference {
        union {
            sphere { center [0, 1, 0] radius 1 }
            torus { center [0, 1, 0] major_radius 1 minor_radius 0.2 tilt_x 90 }
        }
        sphere { center [0, 1, 1] radius 0.7 }
    }
}
)";
        const auto csg_scene = build_scene_description(detached_ast(text + csg, "csg.hipscene"));
        require(objects<Csg>(csg_scene).size() == 1 &&
                std::holds_alternative<Metal>(objects<Csg>(csg_scene)[0]->material) &&
                leaves(geometry<Csg>(csg_scene).root) == 3, "nested CSG AST survives source ownership changes");
        rejects(text + replace(csg, "material blue", "material missing"), "unknown material");
        rejects_build(text + replace(csg, "minor_radius 0.2", "minor_radius 1.2"), "minor_radius");
        rejects_build(text + replace(csg, "radius 0.7", "radius 0"), "positive");
        const auto compact_csg = parse_scene_description(text + replace(csg, "csg {", "csg{"));
        require(objects<Csg>(compact_csg).size() == 1, "CSG literal allows adjacent punctuation");
        rejects(text + replace(csg, "csg {", "csgfoo {"), "expected '{'");
        rejects(text + replace(csg, "union {", "unionfoo {"), "expected '{'");
        rejects(text + replace(csg, "difference {", "unknown {"), "CSG");
        rejects(text + replace(csg, "        sphere { center [0, 1, 1] radius 0.7 }", ""), "CSG");
        const auto demo = load_scene_description(
            (std::filesystem::path(builtin_scene_path(false)).parent_path() / "csg.hipscene").string());
        std::size_t demo_leaves = 0;
        for (const auto* object : objects<Csg>(demo)) demo_leaves += leaves(std::get<Csg>(object->geometry).root);
        require(objects<Csg>(demo).size() == 4 && demo_leaves == 9 &&
                objects<Mesh>(demo).empty() && objects<ExtrudedText>(demo).empty(), "CSG demo is self-contained");
        const auto text_fixture = std::filesystem::absolute(argv[1]).parent_path() / "text.hipscene";
        const auto loaded_text = load_scene_description(text_fixture.string());
        const auto csg_text_fixture = text_fixture.parent_path() / "csg_text.hipscene";
        const auto csg_text = load_scene_description(csg_text_fixture.string());
        const auto& text_root = geometry<Csg>(csg_text, 1).root;
        const auto& first_text = std::get<ExtrudedText>(text_root.children[0].geometry);
        require(objects<Csg>(csg_text).size() == 2 && objects<ExtrudedText>(csg_text).empty() &&
                first_text.value == "BO" && first_text.font == geometry<ExtrudedText>(loaded_text).font &&
                first_text == std::get<ExtrudedText>(text_root.children[1].geometry),
                "CSG text paths resolve and identical operands remain equal");
        const auto text_operand = replace(text_object, " material blue", "");
        const std::string text_solid = "\ncsg { material blue " + text_operand + " }\n";
        const auto text_ast = detached_ast(text + text_solid, "text-solid.hipscene");
        require(std::get<ExtrudedText>(geometry<Csg>(build_scene_description(text_ast)).root.geometry).value == "BO S",
                "CSG text AST owns its strings after copy and move");
        rejects_build(text + replace(text_solid, "depth 0.2", "depth 0"), "positive");
        rejects_build(text + replace(text_solid, "font.ttf", ""), "must not be empty");
        rejects(text + replace(text_solid, "position [-1, 0.2, 0]", "position [-1, 0.2, 0] material blue"),
                "expected '}'");
        require(geometry<ExtrudedText>(loaded_text).font ==
                    (text_fixture.parent_path() / "fonts" / "example.ttf").lexically_normal().string(),
                "font paths resolve relative to scene");
        rejects_build(text + replace(text_object, "depth 0.2", "depth 0"), "positive");
        rejects_build(text + replace(text_object, "size 1.5", "size -1"), "positive");
        rejects(text + replace(text_object, "material blue", "material missing"), "unknown material");
        rejects_build(text + replace(text_object, "\"BO S\"", "\"\""), "must not be empty");
        rejects(text + replace(text_object, "BO S", std::string("B\0S", 3)), "expected ");
        rejects_build(text + replace(text_object, "font.ttf", ""), "must not be empty");
        rejects_build(text + replace(text_object, "size 1.5", "size 1e-300"), "positive");
        rejects(replace(text, "camera {", "image { size [17, 9] output \"x.bmp\" }\ncamera {"), "camera");
        rejects(replace(text, "camera {", "sampling fixed { count 3 batch 2 }\ncamera {"), "camera");
        rejects(replace(text, "hipscene 1", "hipscene 2"), "invalid.hipscene:1:10: expected ");
        rejects_build(replace(text, "radius 0.3", "radius -0.3"), "positive");
        rejects_build(replace(text, "roughness 0.3", "roughness 1.3"), "roughness");
        rejects(replace(text, "material red\n}", "material missing\n}"), "unknown material");
        rejects(replace(text, "material blue =", "material red ="), "duplicate material");
        rejects(replace(text, "up y", "up x"), "mesh up");
        rejects_build(replace(text, "radiance [3, 4, 5]", "radiance [-1, 4, 5]"), "radiance");
        rejects_build(replace(text, "radius 0.3", "radius 1e300"), "finite float");
        rejects(replace(text, "radius 0.3", "radius .3"), "number");
        rejects(replace(text, "radius 0.3", "radius +0.3"), "number");
        rejects(replace(text, "radius 0.3", "radius nan"), "number");
        rejects(replace(text, "radius 0.3", "radius0.3"), "expected ");
        rejects(replace(text, "material blue =", "materialblue ="), "expected ");
        rejects_build(replace(text, "half_view_height 0.4", "half_view_height 0"), "positive");
        rejects_build(replace(text, "size [8, 6, 10]", "size [8, 0, 10]"), "dimensions");
        rejects_build(replace(text, "floor color [0.2, 0.3, 0.4]", "floor color [1.1, 0.3, 0.4]"), "color");
        rejects_build(replace(text, "fit [1, 1, 1]", "fit [-1, 1, 1]"), "dimensions");
        auto modified_ast = detached_ast(text + text_object, "invalid.hipscene");
        modified_ast.camera.position.x = std::numeric_limits<double>::infinity();
        expect_error([&] { build_scene_description(modified_ast); }, "finite float");
        modified_ast.camera.position.x = 0;
        std::get<scene_ast::Text>(modified_ast.objects.back()).value.assign("B\0S", 3);
        expect_error([&] { build_scene_description(modified_ast); }, "contain NUL");
        rejects(replace(text, "target [0, 2, 0]", "target [0, 2, 12]"), "camera direction");
        rejects(replace(text, "minor_radius 0.1", "minor_radius 0.7"), "minor_radius");
        rejects(text + "garbage", "expected ");
        rejects(text + "catcrystal", "expected ");
        rejects(text.substr(0, text.rfind('}')), "expected '}'");
        auto overridden = classic;
        override_scene_mesh(overridden, "replacement.ply", MeshUp::z);
        require(geometry<Mesh>(overridden, 1).file == "replacement.ply" &&
                geometry<Mesh>(overridden, 1).position.y == 0.02f &&
                geometry<Mesh>(overridden, 1).kind == SceneMeshKind::ply, "legacy cat replacement");
        Object gold{Sphere{{1, 2, 3}, 0.5f}, Metal{ConstantColor{{0.8f, 0.6f, 0.1f}}, 0.15f}};
        auto matte = gold;
        matte.material = Diffuse{Wood{}};
        const auto prepared_gold = prepare_material(gold.material);
        const auto prepared_matte = prepare_material(matte.material);
        require(std::get<Sphere>(gold.geometry) == std::get<Sphere>(matte.geometry) &&
                prepared_gold.kind == ShaderKind::metal && prepared_gold.roughness == 0.15f &&
                prepared_gold.albedo.x == 0.8f && prepared_matte.kind == ShaderKind::diffuse &&
                prepared_matte.texture == TextureKind::wood,
                "geometry is independent of material and textures survive GPU material conversion");
        std::cout << "Scene description tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene description tests failed: " << error.what() << '\n';
        return 1;
    }
}
