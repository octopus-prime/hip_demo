#include "parsing/parser.h"
#include "parsing/diagnostics.h"
#include "building/scene_description.h"

#include <boost/parser/parser.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace {
namespace bp = boost::parser;
using Position = std::string_view::const_iterator;
using scene_ast::Vector;
using scene_ast::MaterialValue;
using scene_ast::Surface;

struct Camera {
    Position where;
    Vector position, target;
    double half_view_height;
};

struct Room {
    Position where;
    Vector size;
    Surface floor, ceiling, back, left, right;
};

struct NamedMaterial {
    Position where;
    std::string name;
    MaterialValue value;
};

struct RectangleLight {
    Position where;
    std::array<double, 2> center, size;
    Vector radiance;
};

struct TorusLight {
    Position where;
    Vector center;
    double major_radius, minor_radius;
    Vector radiance;
};

struct Sphere {
    Position where;
    Vector center;
    double radius;
    std::string material;
};

struct Torus {
    Position where;
    Vector center;
    double major_radius, minor_radius, tilt_x;
    std::string material;
};

struct Mesh {
    Position where;
    std::string file;
    MeshUp up;
    Vector fit, position;
    std::string material;
};

struct Text {
    Position where;
    std::string font, value;
    double size, depth;
    Vector position;
    std::string material;
};

using Light = std::variant<RectangleLight, TorusLight>;
struct CsgNode {
    Position where;
    std::variant<Sphere, Torus> shape;
    CsgOperation operation = CsgOperation::unite;
    std::vector<CsgNode> children;
};
struct CsgBranch {
    Position where;
    CsgOperation operation;
    std::vector<CsgNode> children;
};
struct Csg {
    Position where;
    std::string material;
    CsgNode root;
};
using Object = std::variant<Sphere, Torus, Mesh, Text, Csg, SceneMeshKind>;

struct ParsedScene {
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
bp::rule<struct scalar_p_, double> scalar_p = "number";
bp::rule<struct vector2_p_, std::array<double, 2>> vector2_p = "2D vector";
bp::rule<struct vector_p_, Vector> vector_p = "3D vector";
bp::rule<struct camera_p_, Camera> camera_p = "camera";
bp::rule<struct surface_p_, Surface> surface_p = "color surface";
bp::rule<struct wood_p_, Surface> wood_p = "wood surface";
bp::rule<struct marble_p_, Surface> marble_p = "marble surface";
bp::rule<struct floor_p_, Surface> floor_p = "floor surface";
bp::rule<struct ceiling_p_, Surface> ceiling_p = "ceiling surface";
bp::rule<struct back_p_, Surface> back_p = "back surface";
bp::rule<struct left_p_, Surface> left_p = "left surface";
bp::rule<struct right_p_, Surface> right_p = "right surface";
bp::rule<struct room_p_, Room> room_p = "room";
bp::rule<struct diffuse_p_, MaterialValue> diffuse_p = "diffuse material";
bp::rule<struct phong_p_, MaterialValue> phong_p = "Phong material";
bp::rule<struct metal_p_, MaterialValue> metal_p = "metal material";
bp::rule<struct glass_p_, MaterialValue> glass_p = "glass material";
bp::rule<struct shader_p_, MaterialValue> shader_p = "material kind";
bp::rule<struct material_p_, NamedMaterial> material_p = "named material";
bp::rule<struct rectangle_light_p_, RectangleLight> rectangle_light_p = "ceiling rectangle light";
bp::rule<struct torus_light_p_, TorusLight> torus_light_p = "torus light";
bp::rule<struct light_p_, Light> light_p = "light";
bp::rule<struct sphere_p_, Sphere> sphere_p = "sphere";
bp::rule<struct torus_p_, Torus> torus_p = "torus";
bp::rule<struct csg_sphere_p_, Sphere> csg_sphere_p = "CSG sphere";
bp::rule<struct csg_torus_p_, Torus> csg_torus_p = "CSG torus";
bp::rule<struct csg_branch_p_, CsgBranch> csg_branch_p = "binary CSG operation";
bp::rule<struct csg_node_p_, CsgNode> csg_node_p = "CSG sphere, torus or operation";
bp::rule<struct csg_p_, Csg> csg_p = "CSG object";
bp::rule<struct mesh_up_p_, MeshUp> mesh_up_p = "mesh up (y or z)";
bp::rule<struct mesh_p_, Mesh> mesh_p = "mesh";
bp::rule<struct text_p_, Text> text_p = "extruded text";
bp::rule<struct builtin_p_, SceneMeshKind> builtin_p = "cat or crystal";
bp::rule<struct object_p_, Object> object_p = "object";
bp::rule<struct scene_p_, ParsedScene> scene_p = "hipscene 1 scene";

auto position_p_def = bp::eps[([](auto& context) { bp::_val(context) = bp::_where(context).begin(); })];
auto identifier_p_def = bp::lexeme[letter >> *identifier_char];
auto string_p_def = bp::lexeme['"' >>
    *((bp::lit('\\') >> bp::char_("\"\\")) | (bp::char_ - bp::char_("\"\\\r\n"))) > '"'];
auto scalar_p_def = bp::lexeme[&(number_syntax >> !(identifier_char | bp::char_("+.-"))) >> bp::double_];
auto vector2_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ']';
auto vector_p_def = bp::lit('[') > scalar_p > ',' > scalar_p > ',' > scalar_p > ']';
auto camera_p_def = position_p >> bp::lit("camera") > '{'
    > bp::lit("position") > vector_p > bp::lit("target") > vector_p
    > keyword("half_view_height") > scalar_p > '}';
auto surface_p_def = bp::lit("color") > vector_p > bp::attr(SurfaceKind::color);
auto wood_p_def = keyword("wood_detail") >> bp::attr(Surface{{}, SurfaceKind::wood});
auto marble_p_def = keyword("marble_detail") >> bp::attr(Surface{{}, SurfaceKind::marble});
auto floor_p_def = surface_p | (keyword("tiles_detail") >> bp::attr(Surface{{}, SurfaceKind::tiles}));
auto ceiling_p_def = surface_p | wood_p;
auto back_p_def = surface_p | wood_p | marble_p;
auto left_p_def = surface_p | (keyword("mandelbrot_detail") >> bp::attr(Surface{{}, SurfaceKind::mandelbrot}));
auto right_p_def = surface_p | (keyword("julia_detail") >> bp::attr(Surface{{}, SurfaceKind::julia}));
auto room_p_def = position_p >> bp::lit("room") > '{' > bp::lit("size") > vector_p
    > keyword("floor") > floor_p > keyword("ceiling") > ceiling_p > keyword("back") > back_p
    > keyword("left") > left_p > keyword("right") > right_p > '}';

auto diffuse_p_def = bp::lit("diffuse") > vector_p > bp::attr(Material::diffuse) > bp::attr(1.0);
auto phong_p_def = bp::lit("phong") > vector_p > bp::attr(Material::phong) > bp::attr(0.0);
auto metal_p_def = bp::lit("metal") > vector_p > bp::attr(Material::metal) > keyword("roughness") > scalar_p;
auto glass_p_def = bp::lit("glass") > vector_p > bp::attr(Material::glass) > bp::attr(0.0);
auto shader_p_def = diffuse_p | phong_p | metal_p | glass_p;
auto material_p_def = position_p >> keyword("material") > identifier_p > '=' > shader_p;
auto rectangle_light_p_def = position_p >> bp::lit("ceiling_rectangle") > '{'
    > bp::lit("center") > vector2_p > bp::lit("size") > vector2_p
    > bp::lit("radiance") > vector_p > '}';
auto torus_light_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > bp::lit("radiance") > vector_p > '}';
auto light_p_def = keyword("light") > (rectangle_light_p | torus_light_p);

auto sphere_p_def = position_p >> bp::lit("sphere") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p
    > keyword("material") > identifier_p > '}';
auto torus_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > keyword("tilt_x") > scalar_p
    > keyword("material") > identifier_p > '}';
auto csg_sphere_p_def = position_p >> bp::lit("sphere") > '{'
    > bp::lit("center") > vector_p > keyword("radius") > scalar_p > bp::attr(std::string{}) > '}';
auto csg_torus_p_def = position_p >> bp::lit("torus") > '{'
    > bp::lit("center") > vector_p > keyword("major_radius") > scalar_p
    > keyword("minor_radius") > scalar_p > keyword("tilt_x") > scalar_p > bp::attr(std::string{}) > '}';
auto csg_branch_p_def = position_p >> (
    (bp::lit("union") >> bp::attr(CsgOperation::unite)) |
    (bp::lit("intersection") >> bp::attr(CsgOperation::intersection)) |
    (bp::lit("difference") >> bp::attr(CsgOperation::difference)))
    > '{' > bp::repeat(2)[csg_node_p] > '}';
auto csg_node_p_def =
    csg_sphere_p[([](auto& context) {
        const auto& shape = bp::_attr(context);
        bp::_val(context) = CsgNode{shape.where, shape, CsgOperation::unite, {}};
    })] |
    csg_torus_p[([](auto& context) {
        const auto& shape = bp::_attr(context);
        bp::_val(context) = CsgNode{shape.where, shape, CsgOperation::unite, {}};
    })] |
    csg_branch_p[([](auto& context) {
        auto& branch = bp::_attr(context);
        bp::_val(context) = CsgNode{branch.where, Sphere{}, branch.operation, std::move(branch.children)};
    })];
auto csg_p_def = position_p >> bp::lit("csg") > '{'
    > keyword("material") > identifier_p > csg_node_p > '}';
auto mesh_up_p_def = (keyword("y") >> bp::attr(MeshUp::y)) | (keyword("z") >> bp::attr(MeshUp::z));
auto mesh_p_def = position_p >> bp::lit("mesh") > '{'
    > bp::lit("file") > string_p > keyword("up") > mesh_up_p > bp::lit("fit") > vector_p
    > bp::lit("position") > vector_p > keyword("material") > identifier_p > '}';
auto text_p_def = position_p >> bp::lit("text") > '{'
    > bp::lit("font") > string_p > bp::lit("value") > string_p
    > keyword("size") > scalar_p > keyword("depth") > scalar_p
    > bp::lit("position") > vector_p > keyword("material") > identifier_p > '}';
auto builtin_p_def = (keyword("cat") >> bp::attr(SceneMeshKind::cat)) |
                     (keyword("crystal") >> bp::attr(SceneMeshKind::crystal));
auto object_p_def = sphere_p | torus_p | mesh_p | text_p | csg_p | builtin_p;
auto scene_p_def = keyword("hipscene") > keyword("1")
    > camera_p > room_p > *material_p > light_p > *object_p > bp::eoi;

BOOST_PARSER_DEFINE_RULES(
    position_p, identifier_p, string_p, scalar_p, vector2_p, vector_p,
    camera_p, surface_p, wood_p, marble_p, floor_p, ceiling_p, back_p, left_p, right_p, room_p,
    diffuse_p, phong_p, metal_p, glass_p, shader_p, material_p, rectangle_light_p, torus_light_p, light_p, sphere_p, torus_p,
    csg_sphere_p, csg_torus_p, csg_branch_p, csg_node_p, csg_p,
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
    const auto convert_csg = [&](auto&& self, const CsgNode& node) -> scene_ast::CsgNode {
        scene_ast::CsgNode result;
        result.where = offset(node.where);
        result.operation = node.operation;
        if (node.children.empty()) {
            result.shape = std::visit([&](const auto& shape) -> std::variant<scene_ast::Sphere, scene_ast::Torus> {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, Sphere>)
                    return scene_ast::Sphere{offset(shape.where), shape.center, shape.radius, {}};
                else
                    return scene_ast::Torus{offset(shape.where), shape.center, shape.major_radius,
                        shape.minor_radius, shape.tilt_x, {}};
            }, node.shape);
        }
        for (const auto& child : node.children) result.children.push_back(self(self, child));
        return result;
    };
    scene_ast::Scene scene;
    scene.text = text;
    scene.source = source;
    scene.camera = {offset(parsed.camera.where), parsed.camera.position, parsed.camera.target,
                    parsed.camera.half_view_height};
    scene.room = {offset(parsed.room.where), parsed.room.size, parsed.room.floor, parsed.room.ceiling, parsed.room.back,
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
            } else if constexpr (std::is_same_v<T, Csg>) {
                return scene_ast::Csg{offset(value.where), std::move(value.material),
                    convert_csg(convert_csg, value.root)};
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
