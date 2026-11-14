#include "surface/text.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void line(std::vector<TextCurve>& curves, double x, double y, double end_x, double end_y) {
    curves.push_back({{x, end_x - x, 0, 0}, {y, end_y - y, 0, 0}});
}

void check_intersections() {
    std::vector<TextCurve> curves;
    line(curves, 0, 0, 3, 0); line(curves, 3, 0, 3, 3);
    line(curves, 3, 3, 0, 3); line(curves, 0, 3, 0, 0);
    line(curves, 1, 1, 1, 2); line(curves, 1, 2, 2, 2);
    line(curves, 2, 2, 2, 1); line(curves, 2, 1, 1, 1);
    TextGlyph glyph;
    glyph.depth = 0.5f;
    glyph.curve_count = static_cast<std::uint32_t>(curves.size());
    TextIntersection hit{};
    const auto intersect = [&](SceneVector origin, SceneVector direction, float minimum = 0.001f, float maximum = 100) {
        return intersect_text(glyph, curves.data(), origin, direction, minimum, maximum, hit);
    };
    require(intersect({0.5f, 0.5f, 2}, {0, 0, -1}) && hit.distance == 1.5f && hit.normal.z == 1, "front cap");
    require(intersect({0.5f, 0.5f, -2}, {0, 0, 2}) && hit.distance == 1 && hit.normal.z == -1, "back cap/non-unit ray");
    require(!intersect({1.5f, 1.5f, 2}, {0, 0, -1}), "counter must remain open");
    require(intersect({0.5f, 0.5f, 0.25f}, {0, 0, 1}) && hit.distance == 0.25f, "inside exit");
    require(intersect({-1, 0.5f, 0.25f}, {1, 0, 0}) && hit.distance == 1 && hit.normal.x == -1, "outer side");
    require(intersect({1.5f, 1.5f, 0.25f}, {1, 0, 0}) && hit.distance == 0.5f && hit.normal.x == -1, "counter side normal");
    require(!intersect({-1, 0.5f, 1}, {1, 0, 0}), "side outside extrusion");
    require(!intersect({0.5f, 0.5f, 2}, {0, 0, -1}, 0.001f, 1.4f), "maximum distance");
    require(intersect({0.5f, 0.5f, 2}, {0, 0, -1}, 1.6f) && hit.distance == 2, "minimum skips entry but keeps exit");
    require(!text_detail::contains(glyph, curves.data(), -1, 3), "horizontal tangent/vertex winding");
    glyph.even_odd = true;
    require(!text_detail::contains(glyph, curves.data(), 1.5, 1.5), "even-odd counter");

    // A curved arch with a straight baseline; the side has an analytic intersection.
    curves = {{{0, 2, 0, 0}, {0, 4, -4, 0}}, {{2, -2, 0, 0}, {0, 0, 0, 0}}};
    glyph.curve_count = 2;
    glyph.normal_sign = -1;
    require(intersect({1, 2, 0.25f}, {0, -1, 0}) && std::abs(hit.distance - 1) < 1e-6f &&
            hit.normal.y > 0.999f, "quadratic side/tangent root");
    require(intersect({1, 0.5f, 2}, {0, 0, -1}), "quadratic cap");
    require(!intersect({1, 1.1f, 2}, {0, 0, -1}), "quadratic cap exclusion");
    curves[0] = {{0, 0, 0, 2}, {0, 3, -3, 0}};
    require(intersect({0.25f, 2, 0.25f}, {0, -1, 0}) && std::abs(hit.distance - 1.25f) < 1e-6f,
            "cubic side");
    glyph.position = {10, 20, -3};
    require(intersect({10.25f, 22, -2.75f}, {0, -1, 0}) && std::abs(hit.distance - 1.25f) < 1e-6f,
            "translated cubic side");
}

void check_roots() {
    double roots[4];
    const double cubic[4]{-0.08, 0.66, -1.5, 1};
    require(text_detail::curve_roots(cubic, roots) == 3 && std::abs(roots[0] - 0.2) < 1e-8 &&
            std::abs(roots[1] - 0.5) < 1e-8 && std::abs(roots[2] - 0.8) < 1e-8, "three cubic roots");
    const double tangent[4]{-0.125, 0.75, -1.5, 1};
    require(text_detail::curve_roots(tangent, roots) == 1 && std::abs(roots[0] - 0.5) < 1e-8, "triple root");
    const double endpoints[4]{0, -1, 1, 0};
    require(text_detail::curve_roots(endpoints, roots) == 2 && roots[0] == 0 && roots[1] == 1, "endpoint roots");
    const double linear[4]{-1e-20, 2e-20, 0, 0};
    require(text_detail::curve_roots(linear, roots) == 1 && roots[0] == 0.5, "scale-independent linear root");
}

void check_font(const char* path) {
    SceneText text{path, "BO S", 2, 0.3f, {1, 2, 3}, {{0.8f, 0.2f, 0.1f}, Material::metal, 0}};
    TextGeometry geometry;
    append_text_geometry(geometry, text, 7);
    require(geometry.glyphs.size() == 3 && geometry.curves.size() > 10, "font outlines and spaces");
    require(geometry.glyphs[0].object_id == 7 && geometry.glyphs[0].maximum.z == 3.3f &&
            geometry.glyphs[0].material.kind == Material::metal, "glyph attributes");
    require(geometry.glyphs[2].position.x > geometry.glyphs[1].position.x, "glyph advances");
    require(std::any_of(geometry.curves.begin(), geometry.curves.end(), [](const TextCurve& c) {
        return c.x[2] != 0 || c.y[2] != 0 || c.x[3] != 0 || c.y[3] != 0;
    }), "font curves are not flattened");
    const auto& letter_o = geometry.glyphs[1];
    const float x = (letter_o.minimum.x + letter_o.maximum.x) * 0.5f;
    const float y = (letter_o.minimum.y + letter_o.maximum.y) * 0.5f;
    TextIntersection hit{};
    require(!intersect_text(letter_o, geometry.curves.data(), {x, y, 5}, {0, 0, -1}, 0.001f, 100, hit),
            "loaded O counter must be open");
    require(intersect_text(letter_o, geometry.curves.data(), {x, y, 3.15f}, {1, 0, 0}, 0.001f, 100, hit) &&
            hit.normal.x < -0.9f, "loaded O counter must have inward-facing wall normals");
    for (const std::string invalid : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                                     std::string("\xf0\x9f"), std::string("\n")}) {
        text.value = invalid;
        bool rejected = false;
        try { TextGeometry output; append_text_geometry(output, text, 0); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "invalid UTF-8/control text accepted");
    }
    text.value = "A";
    text.value += "\xc3\x84";
    TextGeometry unicode;
    append_text_geometry(unicode, text, 0);
    require(unicode.glyphs.size() == 2, "valid UTF-8 glyph lookup");
    text.value = "\xf4\x8f\xbf\xbf";
    bool missing_glyph = false;
    try { TextGeometry output; append_text_geometry(output, text, 0); }
    catch (const std::runtime_error&) { missing_glyph = true; }
    require(missing_glyph, "missing Unicode glyph accepted");
    text.value = "A";
    text.font += ".missing";
    bool rejected = false;
    try { TextGeometry output; append_text_geometry(output, text, 0); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "missing font accepted");
}
}

int main(int argc, char** argv) {
    try {
        check_roots();
        check_intersections();
        if (argc == 2) check_font(argv[1]);
        std::cout << "Text geometry tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Text geometry tests failed: " << error.what() << '\n';
        return 1;
    }
}
