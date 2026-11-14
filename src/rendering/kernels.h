#pragma once

#include "rendering/camera.h"
#include "rendering/sampling.h"
#include "rendering/color.h"

namespace hip_demo {

__global__ void render_kernel(std::uint32_t* pixels, float3* sums, TraceScene scene, int width, int height,
                              int first_sample, int sample_end, int sample_count) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const float3 forward = normalize(vector3(scene.settings->camera_target) - vector3(scene.settings->camera_position));
    const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const float3 up = cross(right, forward);
    const int pixel_index = x + y * width;
    float3 accumulated = first_sample == 0 ? float3{0.0f, 0.0f, 0.0f} : sums[pixel_index];

    for (int sample = first_sample; sample < sample_end; ++sample) {
        accumulated = accumulated + sample_pixel(x, y, width, height, sample, forward, right, up, scene);
    }

    if (sample_end == sample_count) {
        pixels[pixel_index] = encode_color(accumulated / static_cast<float>(sample_count));
    } else {
        sums[pixel_index] = accumulated;
    }
}

__global__ void render_adaptive_kernel(std::uint32_t* pixels, AdaptivePixel* states, TraceScene scene,
                                       int width, int height, int first_sample, int sample_end, int sample_count) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }
    const int pixel_index = x + y * width;
    AdaptivePixel pixel = first_sample == 0 ? AdaptivePixel{} : states[pixel_index];
    if (pixel.done) {
        return;
    }
    const float3 forward = normalize(vector3(scene.settings->camera_target) - vector3(scene.settings->camera_position));
    const float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const float3 up = cross(right, forward);
    for (int sample = first_sample; sample < sample_end; ++sample) {
        add_adaptive_sample(pixel, sample_pixel(x, y, width, height, sample, forward, right, up, scene,
                                                &pixel.specular));
    }
    pixel.done = sample_end == sample_count || adaptive_converged(pixel, &scene.settings->sampling);
    if (pixel.done) {
        pixels[pixel_index] = encode_color(pixel.sum / static_cast<float>(pixel.count));
    }
    states[pixel_index] = pixel;
}

}
