#include "camera.h"

#include <hiprt/hiprt_math.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

bool finite(float3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float3 checked_normalize(float3 value, const char* error)
{
    const float length_squared = hiprt::dot(value, value);
    if (!std::isfinite(length_squared) || length_squared <= 0.0f) {
        throw std::invalid_argument(error);
    }
    return hiprt::normalize(value);
}

}

Camera::Camera(float3 position, float3 target, float3 up, float vertical_fov_degrees)
{
    if (!finite(position) || !finite(target) || !finite(up)) {
        throw std::invalid_argument("Camera vectors must be finite.");
    }
    if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 180.0f) {
        throw std::invalid_argument("Vertical field of view must be finite and between 0 and 180 degrees.");
    }

    const auto forward = checked_normalize(target - position,
        "Camera view direction must have a finite, nonzero squared length in float.");
    const auto up_direction = checked_normalize(up,
        "Camera up vector must have a finite, nonzero squared length in float.");
    const auto right = checked_normalize(hiprt::cross(forward, up_direction),
        "Camera up vector must not be parallel or too close to parallel to its view.");
    const auto corrected_up = hiprt::cross(right, forward);
    const float half_view_height = std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
    if (!std::isfinite(half_view_height) || half_view_height <= 0.0f) {
        throw std::invalid_argument("Camera field of view is not representable on the GPU.");
    }
    camera_ = {position, forward, right, corrected_up, half_view_height};
}

__device__ hiprtRay GpuCamera::generate_ray(unsigned int x, unsigned int y, int width, int height,
                                          float2 offset) const
{
    // Image rows run downward, but camera up points upward.
    const float aspect = static_cast<float>(width) / height;
    const float screen_x = (2.0f * (x + offset.x) / width - 1.0f) * aspect * half_view_height;
    const float screen_y = (1.0f - 2.0f * (y + offset.y) / height) * half_view_height;
    hiprtRay ray{};
    ray.origin = position;
    ray.direction = hiprt::normalize(forward + screen_x * right + screen_y * up);
    ray.minT = 0.001f;
    ray.maxT = 100.0f;
    return ray;
}
