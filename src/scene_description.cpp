#include "scene_description.h"

#include <boost/parser/parser.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace {
namespace bp = boost::parser;
using Position = std::string_view::const_iterator;

struct Image {
    Position where;
    int width, height;
    std::string output;
};

struct Camera {
    Position where;
    SceneVector position, target;
    float half_view_height;
};

struct Room {
    SceneVector size;
    SceneSurface floor, ceiling, back, left, right;
};

struct NamedMaterial {
    Position where;
    std::string name;
    SceneMaterial value;
};

struct RectangleLight {
    Position where;
    std::array<float, 2> center, size;
    SceneVector radiance;
};

struct TorusLight {
    Position where;
    SceneVector center;
    float major_radius, minor_radius;
    SceneVector radiance;
};

struct Sphere {
    Position where;
    SceneVector center;
    float radius;
    std::string material;
};

struct Torus {
    Position where;
    SceneVector center;
    float major_radius, minor_radius, tilt_x;
    std::string material;
};

struct Mesh {
    Position where;
    std::string file;
    MeshUp up;
    SceneVector fit, position;
    std::string material;
};

using Light = std::variant<RectangleLight, TorusLight>;
using Object = std::variant<Sphere, Torus, Mesh, SceneMeshKind>;

struct ParsedScene {
    Image image;
    SceneSampling sampling;
    Camera camera;
    Room room;
    std::vector<NamedMaterial> materials;
    Light light;
    std::vector<Object> objects;
};

struct SceneErrors {
    std::string_view text;
    const std::string& source;

    [[noreturn]] void fail(Position where, const std::string& message) const {
        const auto line = 1 + std::count(text.begin(), where, '\n');
        const auto offset = static_cast<std::size_t>(where - text.begin());
        const auto previous = offset == 0 ? std::string_view::npos : text.rfind('\n', offset - 1);
        const auto column = previous == std::string_view::npos ? offset + 1 : offset - previous;
        throw std::runtime_error(source + ":" + std::to_string(line) + ":" +
                                 std::to_string(column) + ": " + message);
    }

    template <typename Iterator, typename Sentinel>
    bp::error_handler_result operator()(Iterator, Sentinel, const bp::parse_error<Iterator>& error) const {
        fail(error.iter, "expected " + std::string(error.what()));
    }

    template <typename Context, typename Iterator>
    void diagnose(bp::diagnostic_kind, std::string_view message, const Context&, Iterator where) const {
        fail(where, std::string(message));
    }

    template <typename Context>
    void diagnose(bp::diagnostic_kind kind, std::string_view message, const Context& context) const {
        diagnose(kind, message, context, bp::_where(context).begin());
    }
};

template <typename Predicate>
auto checked(Predicate predicate, std::string_view message) {
    return [=](auto& context) {
        if (!predicate(bp::_attr(context))) bp::_report_error(context, message);
        bp::_val(context) = bp::_attr(context);
    };
}

template <auto Member>
const auto assign = [](auto& context) {
    bp::_val(context).*Member = bp::_attr(context);
};

const auto letter = bp::char_('a', 'z') | bp::char_('A', 'Z');
const auto identifier_char = letter | bp::digit | bp::char_('_');
const auto number_syntax = -bp::char_('-') >> +bp::digit >>
    -(bp::lit('.') >> +bp::digit) >> -(bp::char_("eE") >> -bp::char_("+-") >> +bp::digit);

auto keyword(const char* word) {
    return bp::lexeme[bp::lit(word) >> !identifier_char];
}

bp::rule<struct position_p_, Position> position_p = "source position";
bp::rule<struct identifier_p_, std::string> identifier_p = "identifier";
bp::rule<struct string_p_, std::string> string_p = "quoted path";
bp::rule<struct scalar_p_, float> scalar_p = "finite number";
bp::rule<struct positive_p_, float> positive_p = "positive number";
bp::rule<struct integer_p_, int> integer_p = "positive integer";
bp::rule<struct vector2_p_, std::array<float, 2>> vector2_p = "2D vector";
bp::rule<struct vector_p_, SceneVector> vector_p = "3D vector";
bp::rule<struct dimensions_p_, SceneVector> dimensions_p = "positive dimensions";
bp::rule<struct color_p_, SceneVector> color_p = "color";
bp::rule<struct radiance_p_, SceneVector> radiance_p = "radiance";
bp::rule<struct roughness_p_, float> roughness_p = "roughness";
bp::rule<struct image_p_, Image> image_p = "image";
bp::rule<struct fixed_p_, SceneSampling> fixed_p = "fixed sampling";
bp::rule<struct adaptive_p_, SceneSampling> adaptive_p = "adaptive sampling";
bp::rule<struct sampling_p_, SceneSampling> sampling_p = "fixed or adaptive sampling";
bp::rule<struct camera_p_, Camera> camera_p = "camera";
bp::rule<struct surface_p_, SceneSurface> surface_p = "color surface";
bp::rule<struct floor_p_, SceneSurface> floor_p = "floor surface";
bp::rule<struct back_p_, SceneSurface> back_p = "back surface";
bp::rule<struct left_p_, SceneSurface> left_p = "left surface";
bp::rule<struct right_p_, SceneSurface> right_p = "right surface";
bp::rule<struct room_p_, Room> room_p = "room";
bp::rule<struct diffuse_p_, SceneMaterial> diffuse_p = "diffuse material";
bp::rule<struct phong_p_, SceneMaterial> phong_p = "Phong material";
bp::rule<struct metal_p_, SceneMaterial> metal_p = "metal material";
bp::rule<struct glass_p_, SceneMaterial> glass_p = "glass material";
bp::rule<struct shader_p_, SceneMaterial> shader_p = "material kind";
bp::rule<struct material_p_, NamedMaterial> material_p = "named material";
bp::rule<struct rectangle_light_p_, RectangleLight> rectangle_light_p = "ceiling rectangle light";
bp::rule<struct torus_light_p_, TorusLight> torus_light_p = "torus light";
bp::rule<struct light_p_, Light> light_p = "light";
bp::rule<struct sphere_p_, Sphere> sphere_p = "sphere";
bp::rule<struct torus_p_, Torus> torus_p = "torus";
bp::rule<struct mesh_up_p_, MeshUp> mesh_up_p = "mesh up (y or z)";
bp::rule<struct mesh_p_, Mesh> mesh_p = "mesh";
bp::rule<struct builtin_p_, SceneMeshKind> builtin_p = "cat or crystal";
bp::rule<struct object_p_, Object> object_p = "object";
bp::rule<struct scene_p_, ParsedScene> scene_p = "hipscene 1 scene";

auto position_p_def = bp::eps[([](auto& context) { bp::_val(context) = bp::_where(context).begin(); })];
auto identifier_p_def = bp::lexeme[letter >> *identifier_char];
auto string_p_def = bp::lexeme['"' >>
    *((bp::lit('\\') >> bp::char_("\"\\")) | (bp::char_ - bp::char_("\"\\\r\n"))) > '"']
    [checked([](const std::string& value) { return !value.empty() && value.find('\0') == std::string::npos; },
             "paths must not be empty or contain NUL")];
auto scalar_p_def = bp::lexeme[&(number_syntax >> !(identifier_char | bp::char_("+.-"))) >> bp::double_]
    [([](auto& context) {
        const double value = bp::_attr(context);
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
            bp::_report_error(context, "number is outside the finite float range");
        }
        bp::_val(context) = static_cast<float>(value);
    })];
auto positive_p_def = scalar_p[checked([](float value) { return value > 0; }, "value must be positive")];
auto integer_p_def = bp::uint_[([](auto& context) {
    const auto value = bp::_attr(context);
    if (value == 0 || value > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        bp::_report_error(context, "integer must be in [1, INT_MAX]");
    }
    bp::_val(context) = static_cast<int>(value);
})];
auto vector2_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ']';
auto vector_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ',' > scalar_p > ']';
auto dimensions_p_def = vector_p[checked([](SceneVector value) {
    return value.x > 0 && value.y > 0 && value.z > 0;
}, "all dimensions must be positive")];
auto color_p_def = vector_p[checked([](SceneVector value) {
    return value.x >= 0 && value.x <= 1 && value.y >= 0 && value.y <= 1 && value.z >= 0 && value.z <= 1;
}, "color components must be in [0, 1]")];
auto radiance_p_def = vector_p[checked([](SceneVector value) {
    return value.x >= 0 && value.y >= 0 && value.z >= 0;
}, "radiance must be nonnegative")];
auto roughness_p_def = scalar_p[checked([](float value) {
    return value >= 0 && value <= 1;
}, "roughness must be in [0, 1]")];

auto image_p_def = position_p >> keyword("image") > '{'
    > keyword("size") > '[' > integer_p > ',' > integer_p > ']'
    > keyword("output") > string_p > '}';
auto fixed_p_def = keyword("fixed") > '{'
    > keyword("count") > integer_p[assign<&SceneSampling::maximum>]
    > keyword("batch") > integer_p[assign<&SceneSampling::batch>] > '}'
    > bp::eps[([](auto& context) { bp::_val(context).adaptive = false; })];
auto adaptive_p_def = keyword("adaptive") > '{'
    > keyword("min") > integer_p > keyword("specular_min") > integer_p
    > keyword("max") > integer_p > keyword("batch") > integer_p
    > keyword("relative_error") > positive_p > keyword("brightness_floor") > positive_p
    > bp::attr(true) > '}';
auto sampling_p_def = keyword("sampling") > (fixed_p | adaptive_p);
auto camera_p_def = position_p >> keyword("camera") > '{'
    > keyword("position") > vector_p > keyword("target") > vector_p
    > keyword("half_view_height") > positive_p > '}';
auto surface_p_def = keyword("color") > color_p > bp::attr(SurfaceKind::color);
auto floor_p_def = surface_p | (keyword("tiles_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::tiles}));
auto back_p_def = surface_p | (keyword("wood_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::wood}));
auto left_p_def = surface_p | (keyword("mandelbrot_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::mandelbrot}));
auto right_p_def = surface_p | (keyword("julia_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::julia}));
auto room_p_def = keyword("room") > '{' > keyword("size") > dimensions_p
    > keyword("floor") > floor_p > keyword("ceiling") > surface_p > keyword("back") > back_p
    > keyword("left") > left_p > keyword("right") > right_p > '}';

auto diffuse_p_def = keyword("diffuse") > color_p > bp::attr(Material::diffuse) > bp::attr(1.0f);
auto phong_p_def = keyword("phong") > color_p > bp::attr(Material::phong) > bp::attr(0.0f);
auto metal_p_def = keyword("metal") > color_p > bp::attr(Material::metal) > keyword("roughness") > roughness_p;
auto glass_p_def = keyword("glass") > color_p > bp::attr(Material::glass) > bp::attr(0.0f);
auto shader_p_def = diffuse_p | phong_p | metal_p | glass_p;
auto material_p_def = position_p >> keyword("material") > identifier_p > '=' > shader_p;
auto rectangle_light_p_def = position_p >> keyword("ceiling_rectangle") > '{'
    > keyword("center") > vector2_p > keyword("size") > vector2_p
    > keyword("radiance") > radiance_p > '}';
auto torus_light_p_def = position_p >> keyword("torus") > '{'
    > keyword("center") > vector_p > keyword("major_radius") > positive_p
    > keyword("minor_radius") > positive_p > keyword("radiance") > radiance_p > '}';
auto light_p_def = keyword("light") > (rectangle_light_p | torus_light_p);

auto sphere_p_def = position_p >> keyword("sphere") > '{'
    > keyword("center") > vector_p > keyword("radius") > positive_p
    > keyword("material") > identifier_p > '}';
auto torus_p_def = position_p >> keyword("torus") > '{'
    > keyword("center") > vector_p > keyword("major_radius") > positive_p
    > keyword("minor_radius") > positive_p > keyword("tilt_x") > scalar_p
    > keyword("material") > identifier_p > '}';
auto mesh_up_p_def = (keyword("y") >> bp::attr(MeshUp::y)) | (keyword("z") >> bp::attr(MeshUp::z));
auto mesh_p_def = position_p >> keyword("mesh") > '{'
    > keyword("file") > string_p > keyword("up") > mesh_up_p > keyword("fit") > dimensions_p
    > keyword("position") > vector_p > keyword("material") > identifier_p > '}';
auto builtin_p_def = (keyword("cat") >> bp::attr(SceneMeshKind::cat)) |
                     (keyword("crystal") >> bp::attr(SceneMeshKind::crystal));
auto object_p_def = sphere_p | torus_p | mesh_p | builtin_p;
auto scene_p_def = keyword("hipscene") > keyword("1")
    > image_p > sampling_p > camera_p > room_p > *material_p > light_p > *object_p > bp::eoi;

BOOST_PARSER_DEFINE_RULES(
    position_p, identifier_p, string_p, scalar_p, positive_p, integer_p, vector2_p, vector_p,
    dimensions_p, color_p, radiance_p, roughness_p, image_p, fixed_p, adaptive_p, sampling_p,
    camera_p, surface_p, floor_p, back_p, left_p, right_p, room_p, diffuse_p, phong_p, metal_p,
    glass_p, shader_p, material_p, rectangle_light_p, torus_light_p, light_p, sphere_p, torus_p,
    mesh_up_p, mesh_p, builtin_p, object_p, scene_p);

SceneDescription describe_scene(const ParsedScene& parsed, const SceneErrors& errors) {
    SceneDescription scene;
    scene.width = parsed.image.width;
    scene.height = parsed.image.height;
    scene.output = parsed.image.output;
    // Both the BMP file format and the kernels use 32-bit sizes/indices.
    const auto row = (static_cast<std::uint64_t>(scene.width) * 3 + 3) & ~3ull;
    if (row * scene.height + 54 > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uint64_t>(scene.width) * scene.height > std::numeric_limits<int>::max()) {
        errors.fail(parsed.image.where, "image dimensions exceed BMP or renderer limits");
    }
    auto& settings = scene.settings;
    settings.sampling = parsed.sampling;
    settings.camera_position = parsed.camera.position;
    settings.camera_target = parsed.camera.target;
    settings.half_view_height = parsed.camera.half_view_height;
    const double dx = static_cast<double>(parsed.camera.position.x) - parsed.camera.target.x;
    const double dz = static_cast<double>(parsed.camera.position.z) - parsed.camera.target.z;
    if (dx * dx + dz * dz < 1e-12) {
        errors.fail(parsed.camera.where, "camera direction must not be zero or parallel to the Y axis");
    }
    settings.room_size = parsed.room.size;
    settings.surfaces[0] = parsed.room.floor;
    settings.surfaces[1] = parsed.room.ceiling;
    settings.surfaces[2] = parsed.room.back;
    settings.surfaces[3] = parsed.room.left;
    settings.surfaces[4] = parsed.room.right;

    std::map<std::string, SceneMaterial> materials;
    for (const auto& material : parsed.materials) {
        if (!materials.emplace(material.name, material.value).second) {
            errors.fail(material.where, "duplicate material '" + material.name + "'");
        }
    }
    const auto material_reference = [&](const auto& object) {
        const auto found = materials.find(object.material);
        if (found == materials.end()) errors.fail(object.where, "unknown material '" + object.material + "'");
        return found->second;
    };
    const auto check_radii = [&](const auto& torus) {
        if (torus.minor_radius >= torus.major_radius) {
            errors.fail(torus.where, "minor_radius must be less than major_radius");
        }
    };
    auto& light = settings.light;
    std::visit([&](const auto& source) {
        light.radiance = source.radiance;
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>, RectangleLight>) {
            light.center = {source.center[0], settings.room_size.y, source.center[1]};
            light.width = source.size[0];
            light.depth = source.size[1];
            if (light.width <= 0 || light.depth <= 0) errors.fail(source.where, "light dimensions must be positive");
            if (std::abs(light.center.x) + light.width / 2 > settings.room_size.x / 2 ||
                std::abs(light.center.z) + light.depth / 2 > settings.room_size.z / 2) {
                errors.fail(source.where, "ceiling light must fit inside the room");
            }
        } else {
            check_radii(source);
            light.torus = true;
            light.center = source.center;
            light.major_radius = source.major_radius;
            light.minor_radius = source.minor_radius;
        }
    }, parsed.light);
    for (const auto& object : parsed.objects) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Sphere> || std::is_same_v<T, Torus>) {
                ScenePrimitive primitive;
                primitive.center = value.center;
                primitive.material = material_reference(value);
                if constexpr (std::is_same_v<T, Sphere>) {
                    primitive.radius = value.radius;
                } else {
                    check_radii(value);
                    primitive.sphere = false;
                    primitive.major_radius = value.major_radius;
                    primitive.minor_radius = value.minor_radius;
                    const double angle = static_cast<double>(value.tilt_x) * 3.141592653589793 / 180;
                    primitive.tilt_sine = static_cast<float>(std::sin(angle));
                    primitive.tilt_cosine = static_cast<float>(std::cos(angle));
                }
                scene.primitives.push_back(primitive);
            } else {
                SceneMesh mesh;
                if constexpr (std::is_same_v<T, Mesh>) {
                    mesh.file = value.file;
                    mesh.up = value.up;
                    mesh.fit = value.fit;
                    mesh.position = value.position;
                    mesh.material = material_reference(value);
                } else {
                    mesh.kind = value;
                }
                scene.meshes.push_back(mesh);
            }
        }, object);
    }
    if (light.torus) {
        ScenePrimitive emitter;
        emitter.sphere = false;
        emitter.center = light.center;
        emitter.major_radius = light.major_radius;
        emitter.minor_radius = light.minor_radius;
        emitter.material = {{1, 1, 1}, Material::emissive, 0};
        scene.primitives.push_back(emitter);
    }
    return scene;
}
}

SceneDescription parse_scene_description(std::string_view text, const std::string& source) {
    const SceneErrors errors{text, source};
    ParsedScene parsed;
    if (!bp::parse(text, bp::with_error_handler(bp::eps > scene_p, errors), bp::ws, parsed)) {
        errors.fail(text.begin(), "expected scene");
    }
    return describe_scene(parsed, errors);
}

SceneDescription load_scene_description(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open scene: " + path);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) throw std::runtime_error("Failed while reading scene: " + path);
    auto scene = parse_scene_description(text, path);
    const auto directory = std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    for (auto& mesh : scene.meshes) {
        if (mesh.kind == SceneMeshKind::ply) {
            mesh.file = (directory / std::filesystem::path(mesh.file)).lexically_normal().string();
        }
    }
    return scene;
}

std::string builtin_scene_path(bool detail) {
    return (std::filesystem::path(HIP_DEMO_SCENE_DIRECTORY) /
            (detail ? "detail.hipscene" : "classic.hipscene")).string();
}

void override_scene_mesh(SceneDescription& scene, const std::string& path, MeshUp up) {
    SceneMesh* selected = nullptr;
    for (auto& mesh : scene.meshes) {
        if (mesh.kind == SceneMeshKind::cat || mesh.kind == SceneMeshKind::ply) {
            if (selected) throw std::runtime_error("--mesh requires a scene with exactly one cat or PLY mesh.");
            selected = &mesh;
        }
    }
    if (!selected) throw std::runtime_error("--mesh requires a scene containing a cat or PLY mesh.");
    if (selected->kind == SceneMeshKind::cat) {
        selected->fit = {1.05f, 1.55f, 0.9f};
        selected->position = {1.85f, 0.02f, 1.75f};
        selected->material = {{0.78f, 0.36f, 0.16f}, Material::diffuse, 1};
    }
    selected->kind = SceneMeshKind::ply;
    selected->file = path;
    selected->up = up;
}
