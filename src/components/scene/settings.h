#pragma once

#include "components/scene/model.h"
#include "components/scene/gpu_model.h"
#include "components/texture/builder.h"
#include "components/light/builder.h"

namespace hip_demo {
inline GpuSceneSettings prepare_settings(const SceneSettings& settings) {
    GpuSceneSettings result;
    result.camera_position = settings.camera.position;
    result.camera_target = settings.camera.target;
    result.half_view_height = settings.camera.half_view_height;
    result.room_size = settings.room.size;
    result.light = light::prepare(settings.light);
    for (std::size_t i = 0; i < settings.room.textures.size(); ++i)
        result.textures[i] = prepare_texture(settings.room.textures[i]);
    return result;
}

}
