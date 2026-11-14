#include "parsing/parser.h"
#include "parsing/diagnostics.h"
#include "building/scene_description.h"

#include <boost/parser/parser.hpp>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
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

struct Text {
    Position where;
    std::string font, value;
    float size, depth;
    SceneVector position;
    std::string material;
};

using Light = std::variant<RectangleLight, TorusLight>;
using Object = std::variant<Sphere, Torus, Mesh, Text, SceneMeshKind>;

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
        SceneDiagnostics{text, source}.fail(static_cast<std::size_t>(where - text.begin()), message);
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
bp::rule<struct string_p_, std::string> string_p = "quoted string";
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
bp::rule<struct wood_p_, SceneSurface> wood_p = "wood surface";
bp::rule<struct marble_p_, SceneSurface> marble_p = "marble surface";
bp::rule<struct floor_p_, SceneSurface> floor_p = "floor surface";
bp::rule<struct ceiling_p_, SceneSurface> ceiling_p = "ceiling surface";
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
bp::rule<struct text_p_, Text> text_p = "extruded text";
bp::rule<struct builtin_p_, SceneMeshKind> builtin_p = "cat or crystal";
bp::rule<struct object_p_, Object> object_p = "object";
bp::rule<struct scene_p_, ParsedScene> scene_p = "hipscene 1 scene";

auto position_p_def = bp::eps[([](auto& context) { bp::_val(context) = bp::_where(context).begin(); })];
auto identifier_p_def = bp::lexeme[letter >> *identifier_char];
auto string_p_def = bp::lexeme['"' >>
    *((bp::lit('\\') >> bp::char_("\"\\")) | (bp::char_ - bp::char_("\"\\\r\n"))) > '"']
    [checked([](const std::string& value) { return !value.empty() && value.find('\0') == std::string::npos; },
             "strings must not be empty or contain NUL")];
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
auto wood_p_def = keyword("wood_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::wood});
auto marble_p_def = keyword("marble_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::marble});
auto floor_p_def = surface_p | (keyword("tiles_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::tiles}));
auto ceiling_p_def = surface_p | wood_p;
auto back_p_def = surface_p | wood_p | marble_p;
auto left_p_def = surface_p | (keyword("mandelbrot_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::mandelbrot}));
auto right_p_def = surface_p | (keyword("julia_detail") >> bp::attr(SceneSurface{{}, SurfaceKind::julia}));
auto room_p_def = keyword("room") > '{' > keyword("size") > dimensions_p
    > keyword("floor") > floor_p > keyword("ceiling") > ceiling_p > keyword("back") > back_p
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
auto text_p_def = position_p >> keyword("text") > '{'
    > keyword("font") > string_p > keyword("value") > string_p
    > keyword("size") > positive_p > keyword("depth") > positive_p
    > keyword("position") > vector_p > keyword("material") > identifier_p > '}';
auto builtin_p_def = (keyword("cat") >> bp::attr(SceneMeshKind::cat)) |
                     (keyword("crystal") >> bp::attr(SceneMeshKind::crystal));
auto object_p_def = sphere_p | torus_p | mesh_p | text_p | builtin_p;
auto scene_p_def = keyword("hipscene") > keyword("1")
    > image_p > sampling_p > camera_p > room_p > *material_p > light_p > *object_p > bp::eoi;

BOOST_PARSER_DEFINE_RULES(
    position_p, identifier_p, string_p, scalar_p, positive_p, integer_p, vector2_p, vector_p,
    dimensions_p, color_p, radiance_p, roughness_p, image_p, fixed_p, adaptive_p, sampling_p,
    camera_p, surface_p, wood_p, marble_p, floor_p, ceiling_p, back_p, left_p, right_p, room_p,
    diffuse_p, phong_p, metal_p, glass_p, shader_p, material_p, rectangle_light_p, torus_light_p, light_p, sphere_p, torus_p,
    mesh_up_p, mesh_p, text_p, builtin_p, object_p, scene_p);

}

scene_ast::Scene parse_scene_ast(std::string_view text, const std::string& source) {
    const SceneErrors errors{text, source};
    ParsedScene parsed;
    if (!bp::parse(text, bp::with_error_handler(bp::eps > scene_p, errors), bp::ws, parsed)) {
        errors.fail(text.begin(), "expected scene");
    }
    const auto offset = [&](Position where) {
        return static_cast<scene_ast::Position>(where - text.begin());
    };
    scene_ast::Scene scene;
    scene.text = text;
    scene.source = source;
    scene.image = {offset(parsed.image.where), parsed.image.width, parsed.image.height,
                   std::move(parsed.image.output)};
    scene.sampling = parsed.sampling;
    scene.camera = {offset(parsed.camera.where), parsed.camera.position, parsed.camera.target,
                    parsed.camera.half_view_height};
    scene.room = {parsed.room.size, parsed.room.floor, parsed.room.ceiling, parsed.room.back,
                  parsed.room.left, parsed.room.right};
    for (auto& material : parsed.materials) {
        scene.materials.push_back({offset(material.where), std::move(material.name), material.value});
    }
    scene.light = std::visit([&](const auto& light) -> scene_ast::Light {
        if constexpr (std::is_same_v<std::decay_t<decltype(light)>, RectangleLight>) {
            return scene_ast::RectangleLight{offset(light.where), light.center, light.size, light.radiance};
        } else {
            return scene_ast::TorusLight{offset(light.where), light.center, light.major_radius,
                                         light.minor_radius, light.radiance};
        }
    }, parsed.light);
    for (auto& object : parsed.objects) {
        scene.objects.push_back(std::visit([&](auto& value) -> scene_ast::Object {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Sphere>) {
                return scene_ast::Sphere{offset(value.where), value.center, value.radius,
                                          std::move(value.material)};
            } else if constexpr (std::is_same_v<T, Torus>) {
                return scene_ast::Torus{offset(value.where), value.center, value.major_radius,
                                         value.minor_radius, value.tilt_x, std::move(value.material)};
            } else if constexpr (std::is_same_v<T, Mesh>) {
                return scene_ast::Mesh{offset(value.where), std::move(value.file), value.up,
                                        value.fit, value.position, std::move(value.material)};
            } else if constexpr (std::is_same_v<T, Text>) {
                return scene_ast::Text{offset(value.where), std::move(value.font), std::move(value.value),
                                        value.size, value.depth, value.position, std::move(value.material)};
            } else {
                return value;
            }
        }, object));
    }
    return scene;
}

SceneDescription parse_scene_description(std::string_view text, const std::string& source) {
    return build_scene_description(parse_scene_ast(text, source));
}
