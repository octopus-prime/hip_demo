#pragma once

#include "components/texture/description.h"
#include "components/texture/model.h"
#include "components/texture/gpu_model.h"
#include "components/value/builder.h"
#include "components/texture/color/builder.h"
#include "components/texture/tiles/builder.h"
#include "components/texture/wood/builder.h"
#include "components/texture/marble/builder.h"
#include "components/texture/mandelbrot/builder.h"
#include "components/texture/julia/builder.h"
#include <type_traits>
#include <optional>

namespace hip_demo::texture {
inline Model build(const Description& value, scene_ast::Position where, const SceneValues& values) {
    return std::visit([&](const auto& source) -> Model {
        using T = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<T, hip_demo::texture::color::Description>) return hip_demo::texture::color::build(source, where, values);
        else if constexpr (std::is_same_v<T, hip_demo::texture::tiles::Description>) return hip_demo::texture::tiles::build(source);
        else if constexpr (std::is_same_v<T, hip_demo::texture::wood::Description>) return hip_demo::texture::wood::build(source);
        else if constexpr (std::is_same_v<T, hip_demo::texture::marble::Description>) return hip_demo::texture::marble::build(source);
        else if constexpr (std::is_same_v<T, hip_demo::texture::mandelbrot::Description>) return hip_demo::texture::mandelbrot::build(source);
        else {
            static_assert(std::is_same_v<T, hip_demo::texture::julia::Description>);
            return hip_demo::texture::julia::build(source);
        }
    }, value);
}
inline Model build_albedo(scene_ast::Vector color, const std::optional<Description>& texture,
                           scene_ast::Position where, const SceneValues& values) {
    const auto constant = values.color(color, where);
    return texture ? build(*texture, where, values) : Model{ConstantColor{constant}};
}
}
namespace hip_demo {
inline GpuTexture prepare_texture(const Texture& texture) {
    return std::visit([](const auto& value) -> GpuTexture {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ConstantColor>) return {value.color, TextureKind::color};
        else if constexpr (std::is_same_v<T, Tiles>) return {{}, TextureKind::tiles};
        else if constexpr (std::is_same_v<T, Wood>) return {{}, TextureKind::wood};
        else if constexpr (std::is_same_v<T, Marble>) return {{}, TextureKind::marble};
        else if constexpr (std::is_same_v<T, Mandelbrot>) return {{}, TextureKind::mandelbrot};
        else {
            static_assert(std::is_same_v<T, Julia>);
            return {{}, TextureKind::julia};
        }
    }, texture);
}

}
