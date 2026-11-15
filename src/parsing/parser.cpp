#include "parsing/parser.h"
#include "parsing/diagnostics.h"
#include "components/scene/builder.h"
#include "components/scene/parser.h"

#include <type_traits>
#include <utility>
#include <variant>

namespace hip_demo::parsing {
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

BOOST_PARSER_DEFINE_RULES(
    position_p, identifier_p, string_p, scalar_p, vector2_p, vector_p,
    camera_p, surface_p, wood_p, marble_p, tiles_p, mandelbrot_p, julia_p,
    floor_p, ceiling_p, back_p, left_p, right_p, room_p,
    diffuse_p, phong_p, metal_p, glass_p, shader_p, material_p, rectangle_light_p, torus_light_p, light_p, sphere_p, torus_p,
    csg_sphere_p, csg_torus_p, csg_text_p, csg_branch_p, csg_node_p, csg_p,
    mesh_up_p, mesh_p, text_p, builtin_p, object_p, scene_p);

}

using namespace hip_demo::parsing;

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
            result.shape = std::visit([&](const auto& shape)
                -> std::variant<scene_ast::Sphere, scene_ast::Torus, scene_ast::Text> {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, Sphere>)
                    return scene_ast::Sphere{offset(shape.where), shape.center, shape.radius, {}};
                else if constexpr (std::is_same_v<T, Torus>)
                    return scene_ast::Torus{offset(shape.where), shape.center, shape.major_radius,
                        shape.minor_radius, shape.tilt_x, {}};
                else
                    return scene_ast::Text{offset(shape.where), shape.font, shape.value,
                        shape.size, shape.depth, shape.position, {}};
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
