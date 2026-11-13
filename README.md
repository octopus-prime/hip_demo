# HIP Demo

A C++23 GPU path tracer that renders a Cornell-style box with red and green walls, a ceiling area light, three spheres, and three tilted tori. Two executables render the same scene:

- `hip_demo` uses HIP kernels and intersects the objects directly.
- `hiprt_demo` uses HIP RT to traverse bounding boxes for custom primitives. The sphere and torus intersection functions run on the GPU; the room is intersected directly. It checks representative intersections against the direct implementation before rendering.

Both render UHD 4K images (3840 x 2160), use HIP `float3`, default to 1024 samples per pixel, and sample the area light directly. Diffuse indirect bounces show color bleeding; the objects include metal, glass, diffuse, and energy-conserving Phong-style glossy materials. Rendering at full quality can take substantially longer than a low-resolution preview.
Each executable reports GPU render time measured with HIP events around the render kernels. Device selection, scene/BVH setup, HIP RT intersection checks, image transfer, and BMP writing are excluded. Compare runs on the same GPU with the same sample count.

The direct HIP implementation is in `src/hip_demo.hip`, the HIP RT implementation is in `src/hiprt_demo.hip`, and their shared rendering code is in `src/renderer_common.h`.

## Requirements

- CMake 3.25 or newer
- AMD HIP SDK on Windows, or ROCm on Linux/WSL
- `hipcc` available on `PATH`, `HIP_PATH` / `ROCM_PATH` set, or ROCm installed under `C:\Program Files\AMD\ROCm\`
- For `hiprt_demo`, a matching HIP RT installation, including its device bitcode and runtime kernel bundles, in the HIP SDK/ROCm installation

If HIP RT is missing, CMake builds only `hip_demo`. Set `HIP_DEMO_BUILD_HIPRT=OFF` to skip HIP RT explicitly.

CMake detects installed GPU architectures using `amdgpu-arch`. To build for a specific GPU or a machine without a GPU, set `HIP_ARCHITECTURE` explicitly (for example, `gfx1201` for the Radeon RX 9070 XT); separate multiple targets with semicolons. Reconfigure after changing GPUs. At runtime the renderer prefers a compatible discrete GPU over an integrated GPU.

## Build and render

```powershell
cmake -S . -B build
cmake --build build --config Release
cmake --build build --target run --config Release
cmake --build build --target run_hiprt --config Release
```

On Linux or WSL, use the same commands from a shell with the ROCm environment configured. Executables are generated in `build/bin/`; the run targets write `build/hip_demo.bmp` and `build/hiprt_demo.bmp`. The HIP RT runtime and kernel bundles are copied beside the executables and must remain there.

When running either executable directly, the BMP is saved in the current directory by default. Supply an output path and optionally a positive sample count for a quicker preview, for example:

```powershell
.\build\bin\hiprt_demo.exe preview.bmp 32
```
