#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>

struct GpuCamera {
    float3 position;
    float3 forward;
    float3 right;
    float3 up;
    float half_view_height;

    __device__ hiprtRay generate_ray(unsigned int x, unsigned int y, int width, int height,
                                    float2 offset = {0.5f, 0.5f}) const;
};

class Camera {
public:
    Camera(float3 position, float3 target, float3 up, float vertical_fov_degrees);

    GpuCamera get() const noexcept { return camera_; }

private:
    GpuCamera camera_;
};
