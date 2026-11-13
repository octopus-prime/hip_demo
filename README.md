# HIP RT Demo

A C++23 GPU path tracer rendering a Cornell-style box with red and green walls,
a ceiling area light, three spheres, three tilted tori, and a faceted crystal.
HIP RT traverses custom bounding-box primitives for the spheres and tori and
native triangles for the mesh. The room is intersected directly.

The renderer produces UHD 4K BMP images (3840 x 2160) with 1024 samples per
pixel by default. It uses HIP `float3`, direct area-light sampling, indirect
bounces, and metal, glass, diffuse, and energy-conserving Phong-style glossy
materials. GPU render time is reported separately from setup, intersection
checks, image transfer, and BMP writing.

## Requirements

- CMake 3.25 or newer
- AMD HIP SDK 7.1, including HIP RT 3.0, its device bitcode and runtime bundles
- A compatible AMD GPU

On Windows, CMake finds the standard `C:\Program Files\AMD\ROCm\7.1`
installation automatically. For a different installation, set
`HIPCC_EXECUTABLE` to the SDK 7.1 compiler when configuring. A different
version of HIP or HIP RT is rejected.

CMake detects installed GPU architectures using the SDK's `amdgpu-arch`.
To target a specific GPU or build on a machine without one, set
`HIP_ARCHITECTURE` explicitly (for example, `gfx1201` for the Radeon RX 9070 XT);
separate multiple targets with semicolons. Reconfigure after changing GPUs.
At runtime the renderer prefers a compatible discrete GPU over an integrated GPU.

## Build and render

From the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\bin\hiprt_demo.exe
```

Use a fresh build directory or explicitly set `HIPCC_EXECUTABLE` if an existing
build directory is cached with another SDK, for example:

```powershell
cmake -S . -B build "-DHIPCC_EXECUTABLE=C:\Program Files\AMD\ROCm\7.1\bin\hipcc.exe"
```

On Linux or WSL, use the same CMake commands with SDK 7.1 installed.

The BMP is saved as `hiprt_demo.bmp` in the current directory. Supply an output
path and optionally a positive sample count for a quicker preview:

```powershell
.\build\bin\hiprt_demo.exe preview.bmp 32
```

Alternatively, `cmake --build build --target run --config Release` renders
to `build\hiprt_demo.bmp`. The executable checks representative mesh and
custom-primitive intersections, room lighting, and Phong sampling before
rendering.

The build copies HIP RT runtime bundles and, on Windows, the matching HIP,
COMGR and HIP RT runtime DLLs alongside the executable. Keep these files
together: using a different system HIP runtime produced incorrect triangle
hits in testing.
