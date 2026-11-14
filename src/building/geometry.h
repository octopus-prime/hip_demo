#pragma once

#include "scene/geometry.h"
#include "surface/mesh_fit.h"
#include "building/bounds.h"
#include <utility>

namespace hip_demo {

inline PreparedScene prepare_scene(const SceneDescription& description) {
    unsigned int cat_detail_start = 0;
    constexpr float margin = 0.01f;
    const auto object_count = static_cast<unsigned int>(description.primitives.size());
    const auto solid_count = object_count + static_cast<unsigned int>(description.csgs.size());
    TextGeometry text;
    for (std::size_t index = 0; index < description.texts.size(); ++index) {
        append_text_geometry(text, description.texts[index], solid_count + static_cast<unsigned int>(index));
    }
    if (text.glyphs.size() > std::numeric_limits<unsigned int>::max() - solid_count) {
        throw std::runtime_error("Scene exceeds the custom primitive index limit");
    }
    const auto glyph_count = static_cast<unsigned int>(text.glyphs.size());
    const auto custom_count = solid_count + glyph_count;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> text_ranges;
    for (const auto& source : description.csg_texts) {
        const auto first = text.glyphs.size();
        append_text_geometry(text, source, 0);
        if (text.glyphs.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("CSG text exceeds the glyph index limit");
        text_ranges.emplace_back(static_cast<std::uint32_t>(first),
                                 static_cast<std::uint32_t>(text.glyphs.size() - first));
    }
    auto csg_leaves = description.csg_leaves;
    for (auto& leaf : csg_leaves) {
        if (!leaf.text) continue;
        const auto range = text_ranges.at(leaf.text_index);
        leaf.first_glyph = range.first;
        leaf.glyph_count = range.second;
    }
    std::vector<float3> bounds(static_cast<std::size_t>(custom_count) * 2);
    for (unsigned int index = 0; index < object_count; ++index) {
        const auto box = primitive_bounds(description.primitives[index], margin);
        bounds[index * 2] = box[0];
        bounds[index * 2 + 1] = box[1];
    }
    for (std::size_t index = 0; index < description.csgs.size(); ++index) {
        const auto& object = description.csgs[index];
        float3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        const auto extend = [&](const std::array<float3, 2>& box) {
            for (const auto& corner : box)
                if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z))
                    throw std::runtime_error("CSG bounds exceed the finite coordinate range");
            low = {std::min(low.x, box[0].x), std::min(low.y, box[0].y), std::min(low.z, box[0].z)};
            high = {std::max(high.x, box[1].x), std::max(high.y, box[1].y), std::max(high.z, box[1].z)};
        };
        for (std::uint32_t leaf = 0; leaf < object.leaf_count; ++leaf) {
            const auto& operand = csg_leaves[object.first_leaf + leaf];
            if (!operand.text) {
                extend(primitive_bounds(operand.shape, margin));
                continue;
            }
            for (std::uint32_t i = 0; i < operand.glyph_count; ++i) {
                const auto& glyph = text.glyphs[operand.first_glyph + i];
                extend({float3{glyph.minimum.x - margin, glyph.minimum.y - margin, glyph.minimum.z - margin},
                        float3{glyph.maximum.x + margin, glyph.maximum.y + margin, glyph.maximum.z + margin}});
            }
        }
        // An empty text solid still needs a finite dispatch box, but never reports a hit.
        if (low.x == INFINITY) low = high = {};
        bounds[(object_count + index) * 2] = low;
        bounds[(object_count + index) * 2 + 1] = high;
    }
    for (unsigned int index = 0; index < glyph_count; ++index) {
        const auto& glyph = text.glyphs[index];
        const std::size_t offset = (solid_count + index) * 2;
        bounds[offset] = {glyph.minimum.x - margin, glyph.minimum.y - margin, glyph.minimum.z - margin};
        bounds[offset + 1] = {glyph.maximum.x + margin, glyph.maximum.y + margin, glyph.maximum.z + margin};
    }
    std::vector<float4> vertices;
    std::vector<TriangleMaterial> triangle_materials;
    unsigned int mesh_id = solid_count + static_cast<unsigned int>(description.texts.size());
    for (const auto& mesh : description.meshes) {
        SceneMaterial material = mesh.material;
        if (mesh.kind == SceneMeshKind::crystal) {
            for (const uint3 triangle : mesh_triangles) {
                append_triangle(vertices, mesh_points[triangle.x], mesh_points[triangle.y], mesh_points[triangle.z]);
            }
            material = {{0.10f, 0.65f, 0.72f}, Material::phong, 0};
        } else if (mesh.kind == SceneMeshKind::cat) {
            append_ellipsoid(vertices, cat_point(0.0f, 0.57f, 0.0f), {0.34f, 0.56f, 0.28f});
            append_ellipsoid(vertices, cat_point(0.0f, 1.20f, 0.0f), {0.31f, 0.31f, 0.26f});
            append_ear(vertices, -1.0f);
            append_ear(vertices, 1.0f);
            append_tail(vertices);
            cat_detail_start = static_cast<unsigned int>(vertices.size() / 3);
            material = {{0.78f, 0.36f, 0.16f}, Material::diffuse, 1};
            triangle_materials.resize(cat_detail_start, {material, mesh_id});
            append_ellipsoid(vertices, cat_point(-0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
            append_ellipsoid(vertices, cat_point(0.13f, 1.25f, 0.25f), {0.045f, 0.055f, 0.025f});
            append_ellipsoid(vertices, cat_point(0.0f, 1.14f, 0.255f), {0.035f, 0.025f, 0.025f});
            material = {{0.025f, 0.018f, 0.012f}, Material::diffuse, 1};
        } else {
            append_ply_mesh(vertices, mesh.file, mesh.up, mesh.fit, mesh.position);
            cat_detail_start = static_cast<unsigned int>(vertices.size() / 3);
        }
        triangle_materials.resize(vertices.size() / 3, {material, mesh_id++});
    }
    return {description.settings, description.primitives, std::move(text), std::move(bounds),
            std::move(vertices), std::move(triangle_materials), cat_detail_start,
            description.csgs, std::move(csg_leaves), glyph_count};
}

}
