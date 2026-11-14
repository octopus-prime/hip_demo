#pragma once

#include "math/vector.h"
#include <array>
#include <vector>

namespace hip_demo {

constexpr std::array<float3, 6> mesh_points{{
    {0.0f, 1.35f, 1.45f}, {0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 1.95f},
    {-0.50f, 0.50f, 1.45f}, {0.0f, 0.50f, 0.95f}, {0.0f, 0.02f, 1.45f},
}};
constexpr std::array<uint3, 8> mesh_triangles{{
    {0, 2, 1}, {0, 3, 2}, {0, 4, 3}, {0, 1, 4},
    {5, 1, 2}, {5, 2, 3}, {5, 3, 4}, {5, 4, 1},
}};
constexpr float3 cat_position{1.85f, 0.0f, 1.75f};

inline void append_triangle(std::vector<float4>& vertices, float3 a, float3 b, float3 c) {
    vertices.push_back({a.x, a.y, a.z, 0.0f});
    vertices.push_back({b.x, b.y, b.z, 0.0f});
    vertices.push_back({c.x, c.y, c.z, 0.0f});
}

inline void append_ellipsoid(std::vector<float4>& vertices, float3 center, float3 radius) {
    constexpr float diagonal = 0.70710678f;
    constexpr std::array<float2, 8> ring{{
        {1.0f, 0.0f}, {diagonal, diagonal}, {0.0f, 1.0f}, {-diagonal, diagonal},
        {-1.0f, 0.0f}, {-diagonal, -diagonal}, {0.0f, -1.0f}, {diagonal, -diagonal},
    }};
    std::array<std::array<float3, 8>, 3> rings{};
    for (std::size_t row = 0; row < rings.size(); ++row) {
        const float y = row == 0 ? diagonal : row == 1 ? 0.0f : -diagonal;
        const float scale = row == 1 ? 1.0f : diagonal;
        for (std::size_t index = 0; index < ring.size(); ++index) {
            rings[row][index] = {center.x + radius.x * scale * ring[index].x,
                                 center.y + radius.y * y,
                                 center.z + radius.z * scale * ring[index].y};
        }
    }
    const float3 top{center.x, center.y + radius.y, center.z};
    const float3 bottom{center.x, center.y - radius.y, center.z};
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const std::size_t next = (index + 1) % ring.size();
        append_triangle(vertices, top, rings[0][next], rings[0][index]);
        for (std::size_t row = 0; row + 1 < rings.size(); ++row) {
            append_triangle(vertices, rings[row][index], rings[row][next], rings[row + 1][index]);
            append_triangle(vertices, rings[row][next], rings[row + 1][next], rings[row + 1][index]);
        }
        append_triangle(vertices, rings[2][index], rings[2][next], bottom);
    }
}

inline float3 cat_point(float x, float y, float z) {
    return {cat_position.x + x, y, cat_position.z + z};
}

inline void append_ear(std::vector<float4>& vertices, float side) {
    const float3 inner = cat_point(side * 0.07f, 1.37f, 0.13f);
    const float3 outer = cat_point(side * 0.27f, 1.32f, 0.10f);
    const float3 back = cat_point(side * 0.19f, 1.35f, -0.10f);
    const float3 tip = cat_point(side * 0.28f, 1.72f, 0.02f);
    append_triangle(vertices, inner, outer, tip);
    append_triangle(vertices, outer, back, tip);
    append_triangle(vertices, back, inner, tip);
    append_triangle(vertices, inner, back, outer);
}

inline void append_tail(std::vector<float4>& vertices) {
    constexpr std::array<float3, 5> points{{
        {0.23f, 0.16f, 0.0f}, {0.45f, 0.18f, 0.0f}, {0.61f, 0.35f, 0.0f},
        {0.63f, 0.59f, 0.0f}, {0.53f, 0.72f, 0.0f},
    }};
    constexpr std::array<float2, 6> ring{{
        {1.0f, 0.0f}, {0.5f, 0.8660254f}, {-0.5f, 0.8660254f},
        {-1.0f, 0.0f}, {-0.5f, -0.8660254f}, {0.5f, -0.8660254f},
    }};
    std::array<std::array<float3, 6>, points.size()> rings{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const float3 before = points[index == 0 ? 0 : index - 1];
        const float3 after = points[index + 1 == points.size() ? index : index + 1];
        const float dx = after.x - before.x;
        const float dy = after.y - before.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        for (std::size_t side = 0; side < ring.size(); ++side) {
            rings[index][side] = cat_point(points[index].x - 0.075f * dy / length * ring[side].x,
                                           points[index].y + 0.075f * dx / length * ring[side].x,
                                           points[index].z + 0.075f * ring[side].y);
        }
    }
    for (std::size_t side = 0; side < ring.size(); ++side) {
        const std::size_t next = (side + 1) % ring.size();
        append_triangle(vertices, cat_point(points.front().x, points.front().y, points.front().z),
                        rings.front()[side], rings.front()[next]);
        for (std::size_t index = 0; index + 1 < points.size(); ++index) {
            append_triangle(vertices, rings[index][side], rings[index + 1][side], rings[index][next]);
            append_triangle(vertices, rings[index][next], rings[index + 1][side], rings[index + 1][next]);
        }
        append_triangle(vertices, cat_point(points.back().x, points.back().y, points.back().z),
                        rings.back()[next], rings.back()[side]);
    }
}

}
