#include "components/mesh/loading.h"

#include "rply.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct ReadContext {
    PlyMesh mesh;
    char error[512]{};
    const char* validation_error = nullptr;
};

void on_error(p_ply ply, const char* message) {
    if (!ply) {
        return;
    }
    void* data = nullptr;
    ply_get_ply_user_data(ply, &data, nullptr);
    if (data) {
        auto& context = *static_cast<ReadContext*>(data);
        std::snprintf(context.error, sizeof(context.error), "%s", message);
    }
}

int on_vertex(p_ply_argument argument) {
    void* data = nullptr;
    long axis = 0, index = 0;
    ply_get_argument_user_data(argument, &data, &axis);
    ply_get_argument_element(argument, nullptr, &index);
    auto& context = *static_cast<ReadContext*>(data);
    const double value = ply_get_argument_value(argument);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        context.validation_error = "Vertex coordinate is not finite or representable as float";
        return 0;
    }
    context.mesh.vertices[static_cast<std::size_t>(index)][static_cast<std::size_t>(axis)] =
        static_cast<float>(value);
    return 1;
}

int on_face(p_ply_argument argument) {
    void* data = nullptr;
    long face = 0, length = 0, item = 0;
    ply_get_argument_user_data(argument, &data, nullptr);
    ply_get_argument_element(argument, nullptr, &face);
    ply_get_argument_property(argument, nullptr, &length, &item);
    auto& context = *static_cast<ReadContext*>(data);
    if (length != 3) {
        context.validation_error = "PLY faces must contain exactly three vertex indices";
        return 0;
    }
    if (item < 0) {
        return 1;
    }
    const double value = ply_get_argument_value(argument);
    if (!std::isfinite(value) || value < 0 || std::trunc(value) != value ||
        value >= static_cast<double>(context.mesh.vertices.size())) {
        context.validation_error = "PLY face vertex index is invalid";
        return 0;
    }
    context.mesh.triangles[static_cast<std::size_t>(face)][static_cast<std::size_t>(item)] =
        static_cast<std::uint32_t>(value);
    return 1;
}

bool integer_type(e_ply_type type) {
    return type == PLY_INT8 || type == PLY_UINT8 || type == PLY_INT16 || type == PLY_UINT16 ||
           type == PLY_INT32 || type == PLY_UIN32 || type == PLY_CHAR || type == PLY_UCHAR ||
           type == PLY_SHORT || type == PLY_USHORT || type == PLY_INT || type == PLY_UINT;
}

void validate_properties(p_ply ply) {
    for (auto element = ply_get_next_element(ply, nullptr); element;
         element = ply_get_next_element(ply, element)) {
        const char* element_name = nullptr;
        ply_get_element_info(element, &element_name, nullptr);
        for (auto property = ply_get_next_property(element, nullptr); property;
             property = ply_get_next_property(element, property)) {
            const char* name = nullptr;
            e_ply_type type{}, length_type{}, value_type{};
            ply_get_property_info(property, &name, &type, &length_type, &value_type);
            const std::string_view element_name_view{element_name}, property_name{name};
            if (element_name_view == "vertex" &&
                (property_name == "x" || property_name == "y" || property_name == "z") &&
                type == PLY_LIST) {
                throw std::runtime_error("Vertex coordinates must be scalar properties");
            }
            if (element_name_view == "face" &&
                (property_name == "vertex_indices" || property_name == "vertex_index") &&
                (type != PLY_LIST || !integer_type(length_type) || !integer_type(value_type))) {
                throw std::runtime_error("Face vertex indices must be an integer list");
            }
        }
    }
}

}

PlyMesh load_ply_mesh(const std::string& path) {
    ReadContext context;
    try {
        const std::unique_ptr<t_ply_, decltype(&ply_close)> ply{
            ply_open(path.c_str(), on_error, 0, &context), ply_close};
        if (!ply || !ply_read_header(ply.get())) {
            throw std::runtime_error(context.error[0] ? context.error : "Cannot open or read PLY header");
        }
        validate_properties(ply.get());
        const long vertices = ply_set_read_cb(ply.get(), "vertex", "x", on_vertex, &context, 0);
        const long ys = ply_set_read_cb(ply.get(), "vertex", "y", on_vertex, &context, 1);
        const long zs = ply_set_read_cb(ply.get(), "vertex", "z", on_vertex, &context, 2);
        long faces = ply_set_read_cb(ply.get(), "face", "vertex_indices", on_face, &context, 0);
        if (faces == 0) {
            faces = ply_set_read_cb(ply.get(), "face", "vertex_index", on_face, &context, 0);
        }
        if (vertices <= 0 || ys != vertices || zs != vertices || faces <= 0) {
            throw std::runtime_error("PLY requires nonempty vertex x/y/z and triangle face indices");
        }
        if (static_cast<unsigned long long>(vertices) > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<unsigned long long>(faces) > std::numeric_limits<std::uint32_t>::max() / 3u - 8u) {
            throw std::runtime_error("PLY mesh exceeds HIP RT vertex limits");
        }
        context.mesh.vertices.resize(static_cast<std::size_t>(vertices));
        context.mesh.triangles.resize(static_cast<std::size_t>(faces));
        if (!ply_read(ply.get())) {
            throw std::runtime_error(context.validation_error ? context.validation_error :
                                     context.error[0] ? context.error : "Cannot read PLY data");
        }
        return std::move(context.mesh);
    } catch (const std::exception& error) {
        throw std::runtime_error("PLY '" + path + "': " + error.what());
    }
}
