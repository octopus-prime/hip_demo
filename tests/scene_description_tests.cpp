#include "components/scene/builder.h"
#include "parsing/parser.h"
#include "components/material/builder.h"
#include "components/geometry/builder.h"

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
const Geometry& geometry_of(const SceneDescription& scene, std::size_t index = 0) {
    return std::get<Geometry>(objects<Geometry>(scene).at(index)->geometry);
}

template <typename Geometry, typename Material>
const Material& material_of(const SceneDescription& scene, std::size_t index = 0) {
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
                geometry_of<Mesh>(classic, 0).kind == SceneMeshKind::crystal &&
                geometry_of<Mesh>(classic, 1).kind == SceneMeshKind::cat, "classic scene conversion");
        require(std::abs(geometry_of<Torus>(classic).tilt_sine + 0.8660254f) < 1e-6f,
                "tilt_x uses degrees");
        require(objects<Sphere>(detail).size() + objects<Torus>(detail).size() == 3 &&
                std::holds_alternative<Emissive>(detail.objects.back().material) &&
                std::holds_alternative<Tiles>(detail.settings.room.textures[0]) &&
                std::holds_alternative<Wood>(detail.settings.room.textures[1]) &&
                std::holds_alternative<Marble>(detail.settings.room.textures[2]), "detail scene conversion");
        const auto scene = load_scene_description(argv[1]);
        require(objects<Mesh>(scene).size() == 2 && material_of<Mesh, Metal>(scene, 1).roughness == 0.3f &&
                geometry_of<Mesh>(scene, 1).position.x == 2, "independent mesh transforms and materials");
        require(std::filesystem::path(geometry_of<Mesh>(scene).file).is_absolute() &&
                std::filesystem::exists(geometry_of<Mesh>(scene).file), "mesh paths resolve relative to scene");
        std::ifstream file(argv[1]);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const std::string instances = R"(
hipscene 1
camera { position [0, 2, 10] target [0, 2, 0] half_view_height 0.4 }
room {
    size [12, 8, 10]
    floor color [0.7, 0.7, 0.7] ceiling color [0.7, 0.7, 0.7] back color [0.7, 0.7, 0.7]
    left color [0.7, 0.7, 0.7] right color [0.7, 0.7, 0.7]
}
material red = diffuse [0.8, 0.1, 0.1]
material blue = phong [0.1, 0.2, 0.8] texture wood_detail
prototype ball = sphere { center [0, 0, 0] radius 1 material red }
prototype crystal_shape = crystal
sphere { center [0, 1, 3] radius 0.2 material red }
instance { prototype ball }
instance { prototype ball position [2, 3, 4] rotation [30, 90, 45] scale [2, 1, 0.5] material blue }
instance { prototype crystal_shape position [-2, 1, 0] }
)";
        const auto instanced = build_scene_description(detached_ast(instances, "instances.hipscene"));
        require(instanced.objects.size() == 1 && instanced.prototypes.size() == 2 && instanced.instances.size() == 3 &&
                instanced.instances[0].prototype == instanced.instances[1].prototype &&
                instanced.instances[0].transform.scale.x == 1 && !instanced.instances[0].material &&
                instanced.instances[1].transform.position.z == 4 &&
                instanced.instances[1].transform.rotation.y == 90 &&
                instanced.instances[1].transform.scale.z == 0.5f &&
                std::holds_alternative<Phong>(*instanced.instances[1].material) &&
                std::holds_alternative<Wood>(std::get<Phong>(*instanced.instances[1].material).albedo) &&
                std::holds_alternative<Mesh>(instanced.prototypes[1].geometry),
                "prototypes are not placed and instances retain transforms, sharing and material overrides");
        rejects_build(replace(instances, "prototype ball }", "prototype missing }"), "unknown prototype");
        rejects_build(replace(instances, "prototype crystal_shape = crystal", "prototype ball = crystal"), "duplicate prototype");
        rejects_build(replace(instances, "material blue }", "material missing }"), "unknown material");
        rejects_build(replace(instances, "scale [2, 1, 0.5]", "scale [2, 0, 0.5]"), "dimensions");
        rejects_build(replace(instances, "rotation [30, 90, 45]", "rotation [1e50, 90, 45]"), "finite");
        const std::string quadrics = R"(
ellipsoid { center [0, 2, 0] radii [0.75, 1.5, 0.5] material red }
cylinder { center [4, 2, 0] radius 1 height 2 material red }
cone { center [8, 2, 0] radius 1 height 2 material red }
paraboloid { center [12, 2, 0] radius 1 height 2 material red }
hyperboloid { center [16, 2, 0] radius 0.6 axial_radius 1 height 2 material red }
)";
        const auto quadric_ast = detached_ast(text + quadrics, "quadrics.hipscene");
        const auto quadric_scene = build_scene_description(quadric_ast);
        require(geometry_of<Ellipsoid>(quadric_scene).radii.y == 1.5f &&
                geometry_of<Cylinder>(quadric_scene).height == 2 &&
                geometry_of<Cone>(quadric_scene).center.x == 8 &&
                geometry_of<Paraboloid>(quadric_scene).radius == 1 &&
                geometry_of<Hyperboloid>(quadric_scene).axial_radius == 1 &&
                std::holds_alternative<Diffuse>(objects<Hyperboloid>(quadric_scene)[0]->material),
                "all quadric component descriptions and material references survive detached AST conversion");
        const GpuGeometry gpu_quadrics[]{prepare_geometry(geometry_of<Sphere>(quadric_scene)),
            prepare_geometry(geometry_of<Ellipsoid>(quadric_scene)), prepare_geometry(geometry_of<Cylinder>(quadric_scene)),
            prepare_geometry(geometry_of<Cone>(quadric_scene)), prepare_geometry(geometry_of<Paraboloid>(quadric_scene)),
            prepare_geometry(geometry_of<Hyperboloid>(quadric_scene))};
        for (const auto& geometry : gpu_quadrics)
            require(geometry.kind == GeometryKind::quadric, "all six CPU shapes lower to the same GPU geometry type");
        rejects_build(text + replace(quadrics, "radii [0.75, 1.5, 0.5]", "radii [0, 1.5, 0.5]"), "dimensions");
        for (const std::string name : {"cylinder", "cone", "paraboloid", "hyperboloid"}) {
            const std::string parameters = name == "hyperboloid" ? "radius 1 axial_radius 1 height 0" : "radius 1 height 0";
            rejects_build(text + "\n" + name + " { center [0, 2, 0] " + parameters + " material red }", "positive");
        }
        rejects_build(text + replace(quadrics, "axial_radius 1", "axial_radius -1"), "positive");
        rejects(text + replace(quadrics, "material red", "material missing"), "unknown material");
        const std::string csg_quadrics = R"(
csg { material red union {
    ellipsoid { center [0, 2, 0] radii [1, 2, 3] }
    difference {
        cylinder { center [0, 2, 0] radius 1 height 2 }
        intersection {
            cone { center [0, 2, 0] radius 1 height 2 }
            union {
                paraboloid { center [0, 2, 0] radius 1 height 2 }
                hyperboloid { center [0, 2, 0] radius 0.6 axial_radius 1 height 2 }
            }
        }
    }
} }
)";
        const auto csg_quadric_scene = build_scene_description(detached_ast(text + csg_quadrics, "csg-quadrics.hipscene"));
        const auto& csg_root = geometry_of<Csg>(csg_quadric_scene).root;
        require(std::holds_alternative<Ellipsoid>(csg_root.children[0].geometry) &&
                std::holds_alternative<Cylinder>(csg_root.children[1].children[0].geometry) &&
                std::holds_alternative<Cone>(csg_root.children[1].children[1].children[0].geometry) &&
                std::holds_alternative<Paraboloid>(csg_root.children[1].children[1].children[1].children[0].geometry) &&
                std::holds_alternative<Hyperboloid>(csg_root.children[1].children[1].children[1].children[1].geometry),
                "all new quadric shapes survive CSG parsing and building");
        rejects_build(text + replace(csg_quadrics, "axial_radius 1", "axial_radius 0"), "positive");
        rejects_build(text + replace(csg_quadrics, "radii [1, 2, 3]", "radii [1, 0, 3]"), "dimensions");
        rejects(text + replace(csg_quadrics, "height 2 }", "height 2 material red }"), "}");
        const auto reject_gpu = [&](const auto& geometry, const char* reason) {
            try { (void)prepare_geometry(geometry); }
            catch (const std::runtime_error& error) {
                require(std::string(error.what()).find(reason) != std::string::npos, "explicit GPU quadric validation error");
                return;
            }
            throw std::runtime_error("Invalid GPU quadric accepted");
        };
        reject_gpu(Ellipsoid{{}, {1, std::numeric_limits<float>::quiet_NaN(), 1}}, "finite");
        reject_gpu(Cylinder{{}, 1, 0}, "dimensions");
        reject_gpu(Sphere{{1e20f, 0, 0}, 1}, "bounds");
        reject_gpu(Hyperboloid{{}, 1, 1e-30f, 1e20f}, "waist");
        const auto compact = parse_scene_description(replace(text, "camera {", "camera{"));
        require(compact.settings.camera.position.z == 12, "punctuation does not require whitespace");
        require(std::holds_alternative<Wood>(compact.settings.room.textures[1]) &&
                std::holds_alternative<Marble>(compact.settings.room.textures[2]),
                "wood ceiling and marble back survive grammar and scene conversion");
        const auto legacy = parse_scene_description(replace(text, "back marble_detail", "back wood_detail"));
        require(std::holds_alternative<Wood>(legacy.settings.room.textures[2]), "existing wood backs remain supported");
        require(objects<hip_demo::Rectangle>(classic).size() == 1 &&
                std::get<ConstantColor>(material_of<hip_demo::Rectangle, Emissive>(classic).radiance).color.x == 12 &&
                std::get<ConstantColor>(std::get<Emissive>(detail.objects.back().material).radiance).color.x == 24,
                "legacy lights become geometry with their own emitted radiance");
        const auto light_begin = text.find("light ceiling_rectangle");
        const auto light_end = text.find('}', light_begin) + 1;
        require(light_begin != std::string::npos && light_end > light_begin, "legacy light fixture");
        auto without_light = text;
        without_light.erase(light_begin, light_end - light_begin);
        const auto no_light = parse_scene_description(without_light);
        require(no_light.objects.size() + 1 == scene.objects.size() && !parse_scene_ast(without_light).light,
                "scenes do not require a separate light block");
        const auto emissive_text = replace(without_light, "material red = diffuse [0.8, 0.2, 0.1]",
            "material red = emissive [12, 8, 3]") +
            "\nrectangle { center [0, 5, 0] width 2 depth 1 material red }\n";
        const auto emissive_scene = parse_scene_description(emissive_text);
        const auto emission = prepare_material(objects<Sphere>(emissive_scene)[0]->material);
        require(emission.kind == ShaderKind::emissive && emission.albedo.x == 12 && emission.albedo.y == 8 &&
                std::holds_alternative<Emissive>(objects<hip_demo::Rectangle>(emissive_scene)[0]->material),
                "HDR emissive material is shared by ordinary geometry");
        rejects_build(replace(emissive_text, "emissive [12, 8, 3]", "emissive [-1, 8, 3]"), "radiance");
        rejects_build(replace(emissive_text, "emissive [12, 8, 3]", "emissive [1e300, 8, 3]"), "finite float");
        rejects_build(replace(emissive_text, "width 2", "width 0"), "positive");
        auto emission_ast = detached_ast(emissive_text, "invalid.hipscene");
        std::get<hip_demo::material::emissive::Description>(emission_ast.materials[0].value).radiance.x =
            std::numeric_limits<double>::quiet_NaN();
        expect_error([&] { build_scene_description(emission_ast); }, "finite float");
        rejects(replace(text, "ceiling wood_detail", "ceiling marble_detail"), "ceiling surface");
        rejects(replace(text, "back marble_detail", "back marble_detail_extra"), "back surface");
        const std::string text_object = R"(
text { font "font.ttf" value "BO S" size 1.5 depth 0.2 position [-1, 0.2, 0] material blue }
)";
        const auto escaped = parse_scene_description(text + replace(text_object, "font.ttf", R"(dir\\a\"b.ttf)"));
        require(geometry_of<ExtrudedText>(escaped).font == "dir\\a\"b.ttf", "quoted string escapes");
        const auto with_text = parse_scene_description(text + text_object);
        require(objects<ExtrudedText>(with_text).size() == 1 && geometry_of<ExtrudedText>(with_text).value == "BO S" &&
                geometry_of<ExtrudedText>(with_text).size == 1.5f && geometry_of<ExtrudedText>(with_text).depth == 0.2f &&
                geometry_of<ExtrudedText>(with_text).position.x == -1 &&
                std::holds_alternative<Metal>(objects<ExtrudedText>(with_text)[0]->material),
                "text grammar and conversion");
        const auto owned_ast = detached_ast(text + text_object, "owned.hipscene");
        require(owned_ast.text == text + text_object && owned_ast.source == "owned.hipscene" &&
                owned_ast.text.substr(owned_ast.camera.where, 6) == "camera",
                "AST owns source text, name, and stable source offsets after copy and move");
        const auto owned_scene = build_scene_description(owned_ast);
        const auto& metal_description = std::get<hip_demo::material::metal::Description>(owned_ast.materials.at(1).value);
        require(metal_description.roughness == 0.3 &&
                std::holds_alternative<hip_demo::material::diffuse::Description>(owned_ast.materials.at(0).value),
                "component parser preserves typed shader descriptions and double-valued AST parameters");
        require(objects<Mesh>(owned_scene).size() == 2 &&
                geometry_of<Mesh>(owned_scene).file == "triangle_ascii.ply" &&
                std::holds_alternative<Metal>(objects<Mesh>(owned_scene)[1]->material) &&
                objects<ExtrudedText>(owned_scene).size() == 1 && geometry_of<ExtrudedText>(owned_scene).font == "font.ttf" &&
                geometry_of<ExtrudedText>(owned_scene).value == "BO S" &&
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
                leaves(geometry_of<Csg>(csg_scene).root) == 3, "nested CSG AST survives source ownership changes");
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
        const auto& text_root = geometry_of<Csg>(csg_text, 1).root;
        const auto& first_text = std::get<ExtrudedText>(text_root.children[0].geometry);
        require(objects<Csg>(csg_text).size() == 2 && objects<ExtrudedText>(csg_text).empty() &&
                first_text.value == "BO" && first_text.font == geometry_of<ExtrudedText>(loaded_text).font &&
                first_text == std::get<ExtrudedText>(text_root.children[1].geometry),
                "CSG text paths resolve and identical operands remain equal");
        const auto text_operand = replace(text_object, " material blue", "");
        const std::string text_solid = "\ncsg { material blue " + text_operand + " }\n";
        const auto text_ast = detached_ast(text + text_solid, "text-solid.hipscene");
        require(std::get<ExtrudedText>(geometry_of<Csg>(build_scene_description(text_ast)).root.geometry).value == "BO S",
                "CSG text AST owns its strings after copy and move");
        rejects_build(text + replace(text_solid, "depth 0.2", "depth 0"), "positive");
        rejects_build(text + replace(text_solid, "font.ttf", ""), "must not be empty");
        rejects(text + replace(text_solid, "position [-1, 0.2, 0]", "position [-1, 0.2, 0] material blue"),
                "expected '}'");
        require(geometry_of<ExtrudedText>(loaded_text).font ==
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
        require(geometry_of<Mesh>(overridden, 1).file == "replacement.ply" &&
                geometry_of<Mesh>(overridden, 1).position.y == 0.02f &&
                geometry_of<Mesh>(overridden, 1).kind == SceneMeshKind::ply, "legacy cat replacement");
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
