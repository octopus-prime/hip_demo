#pragma once

#include <hip/hip_runtime.h>

namespace hip_demo {

constexpr int image_width = 3840;
constexpr int image_height = 2160;
constexpr int samples_per_pixel = 1024;
// Keep individual 4K kernels short enough for GPU watchdogs.
constexpr int samples_per_batch = 8;
constexpr int adaptive_min_samples = 64;
constexpr int adaptive_specular_min_samples = 256;
constexpr float adaptive_relative_error = 0.05f;
constexpr float adaptive_brightness_floor = 0.05f;
constexpr int maximum_bounces = 8;
constexpr float pi = 3.14159265359f;
constexpr unsigned int scene_object_count = 7;
constexpr float sphere_radius = 0.48f;
constexpr float torus_major_radius = 0.43f;
constexpr float torus_minor_radius = 0.14f;
constexpr float3 camera_position{0.0f, 2.0f, 8.5f};
constexpr float3 camera_target{0.0f, 1.8f, 0.0f};
constexpr float room_half_width = 3.0f;
constexpr float room_height = 4.0f;
constexpr float room_half_depth = 2.5f;
constexpr float light_half_width = 0.9f;
constexpr float light_half_depth = 0.7f;
constexpr float light_center_z = -0.4f;
constexpr float phong_exponent = 40.0f;
constexpr float phong_specular_reflectance = 0.9f;
constexpr float detail_sphere_radius = 0.72f;
constexpr float detail_torus_major_radius = 1.05f;
constexpr float detail_torus_minor_radius = 0.12f;
constexpr float3 detail_torus_center{0.0f, 3.48f, -0.35f};
constexpr float3 detail_camera_position{0.0f, 2.1f, 6.8f};
constexpr float3 detail_camera_target{0.0f, 1.75f, 0.0f};
constexpr unsigned int detail_object_count = 3;

}
