#include "geometry/text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void check_freetype(FT_Error error, const std::string& operation) {
    if (error) throw std::runtime_error(operation + ": FreeType error " + std::to_string(error));
}

struct LibraryDeleter {
    void operator()(FT_Library library) const { FT_Done_FreeType(library); }
};
struct FaceDeleter {
    void operator()(FT_Face face) const { FT_Done_Face(face); }
};

std::vector<FT_ULong> decode_utf8(const std::string& text) {
    std::vector<FT_ULong> result;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        FT_ULong code = first;
        int extra = 0;
        if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; extra = 1; }
        else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; extra = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { code = first & 0x07; extra = 3; }
        else if (first >= 0x80) throw std::runtime_error("Text contains invalid UTF-8");
        const int length = extra;
        while (extra-- > 0) {
            if (index == text.size()) throw std::runtime_error("Text contains truncated UTF-8");
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xc0) != 0x80) throw std::runtime_error("Text contains invalid UTF-8");
            code = (code << 6) | (byte & 0x3f);
        }
        if ((length == 1 && code < 0x80) || (length == 2 && code < 0x800) ||
            (length == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) throw std::runtime_error("Text contains invalid UTF-8");
        if (code < 0x20 || code == 0x7f) throw std::runtime_error("Text must be a single line without control characters");
        result.push_back(code);
    }
    return result;
}

struct OutlineBuilder {
    std::vector<TextCurve> curves;
    FT_Vector current{}, start{};
    float scale;
    float min_x = std::numeric_limits<float>::infinity(), min_y = min_x;
    float max_x = -min_x, max_y = -min_x;
    bool contour = false;
    std::exception_ptr error;

    template <typename Function>
    int invoke(Function function) noexcept {
        try {
            function();
            return 0;
        } catch (...) {
            // Never unwind through the C FreeType callback frames.
            error = std::current_exception();
            return 1;
        }
    }

    void append(FT_Vector a, FT_Vector b, FT_Vector c, int degree) {
        const FT_Vector points[4]{current, a, b, c};
        float x[4]{}, y[4]{};
        for (int i = 0; i <= degree; ++i) {
            x[i] = static_cast<float>(points[i].x) * scale;
            y[i] = static_cast<float>(points[i].y) * scale;
            min_x = std::min(min_x, x[i]); max_x = std::max(max_x, x[i]);
            min_y = std::min(min_y, y[i]); max_y = std::max(max_y, y[i]);
        }
        TextCurve curve;
        curve.x[0] = x[0]; curve.y[0] = y[0];
        curve.x[1] = static_cast<float>(degree) * (x[1] - x[0]);
        curve.y[1] = static_cast<float>(degree) * (y[1] - y[0]);
        if (degree >= 2) {
            const float factor = degree == 2 ? 1.0f : 3.0f;
            curve.x[2] = factor * (x[0] - 2 * x[1] + x[2]);
            curve.y[2] = factor * (y[0] - 2 * y[1] + y[2]);
        }
        if (degree == 3) {
            curve.x[3] = -x[0] + 3 * x[1] - 3 * x[2] + x[3];
            curve.y[3] = -y[0] + 3 * y[1] - 3 * y[2] + y[3];
        }
        curves.push_back(curve);
        current = points[degree];
    }

    void close() {
        if (contour && (current.x != start.x || current.y != start.y)) append(start, {}, {}, 1);
    }
};
}

void append_text_geometry(TextGeometry& geometry, const hip_demo::ExtrudedText& text,
                          std::uint32_t object_id, GpuMaterial material) {
    if (!std::isfinite(text.size) || text.size <= 0 || !std::isfinite(text.depth) || text.depth <= 0 ||
        !std::isfinite(text.position.x) || !std::isfinite(text.position.y) || !std::isfinite(text.position.z)) {
        throw std::runtime_error("Text size/depth must be positive and its position finite");
    }
    const auto codes = decode_utf8(text.value);
    if (codes.empty()) throw std::runtime_error("Text must not be empty");
    FT_Library raw_library = nullptr;
    check_freetype(FT_Init_FreeType(&raw_library), "Initializing FreeType");
    const std::unique_ptr<FT_LibraryRec_, LibraryDeleter> library(raw_library);
    FT_Face raw_face = nullptr;
    check_freetype(FT_New_Face(library.get(), text.font.c_str(), 0, &raw_face), "Loading font '" + text.font + "'");
    const std::unique_ptr<FT_FaceRec_, FaceDeleter> face(raw_face);
    if (!FT_IS_SCALABLE(face) || face->units_per_EM == 0) throw std::runtime_error("Text requires a scalable outline font");
    check_freetype(FT_Select_Charmap(face.get(), FT_ENCODING_UNICODE), "Selecting the font Unicode charmap");
    const float scale = text.size / face->units_per_EM;
    float advance = 0;
    FT_UInt previous = 0;
    for (const FT_ULong code : codes) {
        const FT_UInt index = FT_Get_Char_Index(face.get(), code);
        if (!index) throw std::runtime_error("Font '" + text.font + "' has no glyph for code point " + std::to_string(code));
        if (previous && FT_HAS_KERNING(face)) {
            FT_Vector kerning{};
            check_freetype(FT_Get_Kerning(face.get(), previous, index, FT_KERNING_UNSCALED, &kerning), "Reading font kerning");
            advance += static_cast<float>(kerning.x) * scale;
        }
        check_freetype(FT_Load_Glyph(face.get(), index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP),
                       "Loading font outline");
        if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) throw std::runtime_error("Glyph has no scalable outline");
        auto& outline = face->glyph->outline;
        check_freetype(FT_Outline_Check(&outline), "Validating font outline");
        if (outline.n_contours > 0) {
            OutlineBuilder builder;
            builder.scale = scale;
            FT_Outline_Funcs callbacks{};
            callbacks.move_to = [](const FT_Vector* to, void* data) {
                auto& b = *static_cast<OutlineBuilder*>(data);
                return b.invoke([&] { b.close(); b.current = b.start = *to; b.contour = true; });
            };
            callbacks.line_to = [](const FT_Vector* to, void* data) {
                auto& b = *static_cast<OutlineBuilder*>(data);
                return b.invoke([&] { b.append(*to, {}, {}, 1); });
            };
            callbacks.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* data) {
                auto& b = *static_cast<OutlineBuilder*>(data);
                return b.invoke([&] { b.append(*control, *to, {}, 2); });
            };
            callbacks.cubic_to = [](const FT_Vector* a, const FT_Vector* b, const FT_Vector* to, void* data) {
                auto& builder = *static_cast<OutlineBuilder*>(data);
                return builder.invoke([&] { builder.append(*a, *b, *to, 3); });
            };
            const auto error = FT_Outline_Decompose(&outline, &callbacks, &builder);
            if (builder.error) std::rethrow_exception(builder.error);
            check_freetype(error, "Decomposing font outline");
            builder.close();
            if (geometry.curves.size() + builder.curves.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("Text exceeds the curve index limit");
            }
            TextGlyph glyph;
            glyph.position = {text.position.x + advance, text.position.y, text.position.z};
            glyph.minimum = {glyph.position.x + builder.min_x, glyph.position.y + builder.min_y, text.position.z};
            glyph.maximum = {glyph.position.x + builder.max_x, glyph.position.y + builder.max_y, text.position.z + text.depth};
            if (!std::isfinite(glyph.minimum.x) || !std::isfinite(glyph.minimum.y) ||
                !std::isfinite(glyph.maximum.x) || !std::isfinite(glyph.maximum.y) ||
                !std::isfinite(glyph.maximum.z) || glyph.maximum.z <= glyph.minimum.z) {
                throw std::runtime_error("Text geometry exceeds the finite coordinate range or depth precision");
            }
            glyph.material = material;
            glyph.depth = text.depth;
            glyph.first_curve = static_cast<std::uint32_t>(geometry.curves.size());
            glyph.curve_count = static_cast<std::uint32_t>(builder.curves.size());
            glyph.object_id = object_id;
            glyph.normal_sign = FT_Outline_Get_Orientation(&outline) == FT_ORIENTATION_TRUETYPE ? -1.0f : 1.0f;
            glyph.even_odd = (outline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0;
            geometry.curves.insert(geometry.curves.end(), builder.curves.begin(), builder.curves.end());
            geometry.glyphs.push_back(glyph);
        }
        advance += static_cast<float>(face->glyph->advance.x) * scale;
        previous = index;
    }
}
