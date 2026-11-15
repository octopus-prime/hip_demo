#pragma once

#include "components/texture/gpu_model.h"
#include "components/light/gpu_model.h"

struct GpuSceneSettings {
    SceneVector camera_position;
    SceneVector camera_target;
    float half_view_height = 0;
    SceneVector room_size;
    GpuTexture textures[5];
    SceneLight light;
};
