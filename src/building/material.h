#pragma once

#include "scene/types.h"
#include "gpu/types.h"

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

inline GpuMaterial prepare_material(const Material& material) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        const auto texture = prepare_texture(value.albedo);
        GpuMaterial result{texture.color, ShaderKind::diffuse, 1, texture.kind};
        if constexpr (std::is_same_v<T, Phong>) {
            result.kind = ShaderKind::phong;
            result.roughness = 0;
        } else if constexpr (std::is_same_v<T, Metal>) {
            result.kind = ShaderKind::metal;
            result.roughness = value.roughness;
        } else if constexpr (std::is_same_v<T, Glass>) {
            result.kind = ShaderKind::glass;
            result.roughness = 0;
        } else if constexpr (std::is_same_v<T, Emissive>) {
            result.kind = ShaderKind::emissive;
            result.roughness = 0;
        } else {
            static_assert(std::is_same_v<T, Diffuse>);
        }
        return result;
    }, material);
}

inline GpuSceneSettings prepare_settings(const SceneSettings& settings) {
    GpuSceneSettings result;
    result.camera_position = settings.camera_position;
    result.camera_target = settings.camera_target;
    result.half_view_height = settings.half_view_height;
    result.room_size = settings.room_size;
    result.light = settings.light;
    for (std::size_t i = 0; i < settings.textures.size(); ++i)
        result.textures[i] = prepare_texture(settings.textures[i]);
    return result;
}

}
