# HIP RT Demo

A C++23 GPU path tracer rendering a Cornell-style box with red and green walls,
a ceiling area light, four spheres, three tilted tori, a faceted crystal, and
a copper-colored low-poly cat. A separate detail scene has a luminous torus,
mirror and glass spheres, and an imported mesh.
HIP RT traverses custom bounding-box primitives for the spheres and tori and
native triangles for the meshes. The room is intersected directly.
The polished sphere next to the crystal reflects it like a mirror, while a
separate glass sphere shows transparency and refraction.

The renderer produces UHD 4K BMP images (3840 x 2160) with up to 1024 samples
per pixel by default. It uses HIP `float3`, direct area-light sampling, indirect
bounces, and metal, glass, diffuse, and energy-conserving Phong-style glossy
materials. GPU render time is reported separately from setup, image transfer,
and BMP writing.

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

The BMP is saved as `hiprt_demo.bmp` in the current directory. By default,
adaptive sampling checks each pixel after batches of eight samples and stops
when its estimated RGB sampling error is sufficiently low. It uses at least
64 samples, or 256 for pixels that hit glass, metal or Phong materials on a
primary ray. The supplied maximum is always respected, even below these minima.
The renderer reports the actual minimum, average and maximum sample counts.

Supply an output path and optionally a positive maximum sample count for a
quicker preview:

```powershell
.\build\bin\hiprt_demo.exe preview.bmp 32
```

For a fixed-sample comparison, add `--fixed` (also usable without a path):

```powershell
.\build\bin\hiprt_demo.exe reference.bmp 1024 --fixed
```

Adaptive sampling is a heuristic: rare light paths can be missed and stopping
based on the same samples can introduce bias. It does not guarantee identical
quality to a fixed-sample render. GPU render time includes adaptive sampling
and sample-statistics collection, but not setup, image transfer or BMP writing.

### PLY meshes

Pass a PLY file to replace the cat with a mesh:

```powershell
.\build\bin\hiprt_demo.exe preview.bmp 64 --mesh "C:\models\my mesh.ply"
```

The mesh is uniformly scaled to fit the cat's space, centered there and placed
on the floor. Its original orientation is retained by default (Y is up); it uses a diffuse
copper-colored material with flat triangle normals. Without `--mesh`, the
original cat remains.

For a Z-up model such as Artec's `Dragon-ply.ply`, rotate it upright before
fitting by adding `--mesh-up z`:

```powershell
.\build\bin\hiprt_demo.exe --mesh "C:\models\Dragon-ply.ply" --mesh-up z
```

`--mesh-up y` explicitly selects the default orientation.

ASCII and little- or big-endian binary PLY files are supported. Each file must
contain vertex `x`, `y`, `z` coordinates and triangular faces with an integer
`vertex_indices` (or `vertex_index`) list. Non-triangular faces must be
triangulated first. Additional properties such as colors, texture coordinates
and vertex normals are ignored. Invalid files are reported as errors.
No external model assets are included; check their licenses before use.

Alternatively, `cmake --build build --target run --config Release` renders
to `build\hiprt_demo.bmp`. Rendering does not run the tests.

### Detail scene

Select the separate scene and supply a PLY mesh. For example, with the
Z-up Artec dragon downloaded separately:

```powershell
.\build\bin\hiprt_demo.exe --scene detail --mesh ".\ply\Dragon-ply\Dragon-ply.ply" --mesh-up z
```

The scene fits the mesh in the center of the Cornell box and places a mirror
sphere and a glass sphere alongside it. The suspended emissive torus is the
light source, including for direct lighting, reflections and refractions.
The mesh uses a glossy Phong material. Its fitting dimensions differ from
the classic scene, and large meshes need additional GPU memory and render time.
The original scene remains the default and is also selectable with
`--scene classic`.

## Tests

The separate GPU test executable checks adaptive sampling, mesh and custom-primitive intersections,
room lighting, Phong sampling, reflection, glass refraction, and cat visibility
without rendering an image. CTest additionally checks PLY parsing, imported
mesh intersections and the detail scene's materials and torus lighting:

```powershell
.\build\bin\hiprt_tests.exe
```

It uses the same scene and GPU selection as the renderer and returns a nonzero
exit code on failure. Run it after changing the GPU, driver or SDK, since the
renderer no longer performs these checks automatically. After building, it can
also be run with `ctest --test-dir build -C Release --output-on-failure`.

The build copies HIP RT runtime bundles and, on Windows, the matching HIP,
COMGR and HIP RT runtime DLLs alongside the executable. Keep these files
together: using a different system HIP runtime produced incorrect triangle
hits in testing.

PLY support uses RPly 1.1.3 by Diego Nehab, under the MIT license. Its original
source and notices are in [third_party/rply](third_party/rply); the build also
copies `RPLY-LICENSE.txt` next to the executables. Include that notice when
redistributing the binaries.
