#pragma once

#include "components/scene/prepared.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hip_demo {

inline double emitter_triangle_area(const std::vector<float4>& vertices, unsigned int triangle) {
    const auto a = vertices.at(static_cast<std::size_t>(triangle) * 3);
    const auto b = vertices.at(static_cast<std::size_t>(triangle) * 3 + 1);
    const auto c = vertices.at(static_cast<std::size_t>(triangle) * 3 + 2);
    const double ux = double(b.x) - a.x, uy = double(b.y) - a.y, uz = double(b.z) - a.z;
    const double vx = double(c.x) - a.x, vy = double(c.y) - a.y, vz = double(c.z) - a.z;
    const double x = uy * vz - uz * vy, y = uz * vx - ux * vz, z = ux * vy - uy * vx;
    const double area = std::hypot(x, y, z) / 2;
    if (!std::isfinite(area)) throw std::runtime_error("Emitter triangle area must be finite");
    if (area == 0) return 0;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
    const float normal_squared = fx * fx + fy * fy + fz * fz;
    if (!std::isfinite(normal_squared) || normal_squared <= 0)
        throw std::runtime_error("Emitter triangle normal exceeds the usable float range");
    return area;
}

inline void prepare_emitters(PreparedScene& scene,
                             unsigned int first_rectangle_id = std::numeric_limits<unsigned int>::max()) {
    constexpr double tau_squared = 39.478417604357434;
    scene.emitters.clear();
    scene.emitter_triangles.clear();
    for (auto& primitive : scene.primitives) primitive.material.sampled_emitter = false;
    for (auto& triangle : scene.triangle_materials) triangle.material.sampled_emitter = false;
    for (std::size_t i = 0; i < scene.primitives.size(); ++i) {
        const auto& primitive = scene.primitives[i];
        if (primitive.material.kind != ShaderKind::emissive || primitive.geometry.kind != GeometryKind::torus)
            continue;
        const auto& torus = primitive.geometry.torus;
        for (const double radius : {double(torus.major_radius) - torus.minor_radius,
                                   double(torus.major_radius) + torus.minor_radius}) {
            const double jacobian = tau_squared * torus.minor_radius * radius;
            const float pdf = static_cast<float>(1 / jacobian);
            if (!std::isfinite(jacobian) || jacobian <= 0 || !std::isfinite(pdf) || pdf <= 0)
                throw std::runtime_error("Torus emitter PDF exceeds the usable float range");
        }
        scene.emitters.push_back({EmitterKind::torus, static_cast<unsigned int>(i),
                                  static_cast<unsigned int>(i), 0, 0});
    }
    if (scene.vertices.size() / 3 != scene.triangle_materials.size() ||
        scene.triangle_materials.size() > std::numeric_limits<unsigned int>::max())
        throw std::runtime_error("Emitter preparation requires matching, indexable mesh buffers");
    for (std::size_t first = 0; first < scene.triangle_materials.size();) {
        const auto id = scene.triangle_materials[first].object_id;
        auto end = first + 1;
        while (end < scene.triangle_materials.size() && scene.triangle_materials[end].object_id == id) ++end;
        const auto cdf_first = static_cast<unsigned int>(scene.emitter_triangles.size());
        bool emissive = false;
        double total = 0;
        for (auto i = first; i < end; ++i) {
            if (scene.triangle_materials[i].material.kind != ShaderKind::emissive) continue;
            emissive = true;
            const auto area = emitter_triangle_area(scene.vertices, static_cast<unsigned int>(i));
            if (area == 0) continue;
            if (total + area == total)
                throw std::runtime_error("Emitter triangle areas exceed the cumulative distribution range");
            total += area;
            scene.emitter_triangles.push_back({static_cast<unsigned int>(i), total});
        }
        if (emissive) {
            const float pdf = static_cast<float>(1 / total);
            if (!std::isfinite(total) || total <= 0 || !std::isfinite(pdf) || pdf <= 0)
                throw std::runtime_error("Emissive mesh must have positive, usable surface area");
            const auto count = static_cast<unsigned int>(scene.emitter_triangles.size()) - cdf_first;
            if (id >= first_rectangle_id) {
                if (end - first != 2 || count != 2)
                    throw std::runtime_error("Rectangle emitter requires two nondegenerate triangles");
                scene.emitters.push_back({EmitterKind::rectangle, id, static_cast<unsigned int>(first), 2, total});
                scene.emitter_triangles.resize(cdf_first);
            } else scene.emitters.push_back({EmitterKind::mesh, id, cdf_first, count, total});
        }
        first = end;
    }
    if (scene.emitters.size() > std::numeric_limits<unsigned int>::max())
        throw std::runtime_error("Scene exceeds the emitter index limit");
    for (const auto& emitter : scene.emitters) {
        double minimum_pdf = 0;
        if (emitter.kind == EmitterKind::torus) {
            const auto& torus = scene.primitives[emitter.first].geometry.torus;
            minimum_pdf = 1 / (tau_squared * torus.minor_radius *
                              (double(torus.major_radius) + torus.minor_radius));
        } else minimum_pdf = 1 / emitter.area;
        const float joint_pdf = static_cast<float>(minimum_pdf / scene.emitters.size());
        if (!std::isfinite(joint_pdf) || joint_pdf <= 0)
            throw std::runtime_error("Emitter selection PDF exceeds the usable float range");
        if (emitter.kind == EmitterKind::torus) {
            scene.primitives[emitter.first].material.sampled_emitter = true;
        } else if (emitter.kind == EmitterKind::rectangle) {
            for (unsigned int i = 0; i < 2; ++i)
                scene.triangle_materials[emitter.first + i].material.sampled_emitter = true;
        } else {
            for (unsigned int i = 0; i < emitter.count; ++i)
                scene.triangle_materials[scene.emitter_triangles[emitter.first + i].triangle]
                    .material.sampled_emitter = true;
        }
    }
}
}
