# HIP Demo

A standalone C++23 HIP ray tracer that renders a metallic sphere and a glass torus on an AMD GPU. The image is written as `hip_demo.ppm` in portable PPM format.

The renderer uses HIP kernels for sphere intersection, torus sphere tracing, reflection, and refraction. It does not depend on HIP RT; HIP RT is a separate library and can be added later for hardware-accelerated scene traversal.

## Requirements

- CMake 3.25 or newer
- AMD HIP SDK on Windows, or ROCm on Linux/WSL
- `hipcc` available on `PATH`, or `HIP_PATH` / `ROCM_PATH` set to the SDK installation

For a Radeon RX 9070 XT, the AMD GPU target is `gfx1201`. If compiler auto-detection does not select it, pass `-DHIP_ARCHITECTURE=gfx1201` during configuration.

## Build and render

```powershell
cmake -S . -B build -DHIP_ARCHITECTURE=gfx1201
cmake --build build --config Release
cmake --build build --target run --config Release
```

On Linux or WSL, use the same commands from a shell with the ROCm environment configured. The generated `build/hip_demo.ppm` can be opened in an image viewer that supports PPM.
