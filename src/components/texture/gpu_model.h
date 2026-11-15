#pragma once

#include "scene/common.h"

struct GpuTexture {
    SceneVector color;
    TextureKind kind = TextureKind::color;
};
