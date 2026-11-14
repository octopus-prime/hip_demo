# HIP RT Demo

A command-line GPU path tracer for AMD GPUs. It renders scenes with spheres,
tori, PLY meshes and extruded text, with reflections, glass, glossy materials
and procedural wall textures. Images are saved as **PNG or BMP**.

## Quick start

From the repository root, with the [requirements](#requirements) installed:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\bin\hiprt_demo.exe preview.png 32
```

This renders the classic scene at **3840 x 2160**, with **at most 32 samples per
pixel**, and saves `preview.png` in the current directory. For a lower-resolution
preview, see [Changing the resolution](#changing-the-resolution).

Running without arguments uses the selected scene's settings:

```powershell
.\build\bin\hiprt_demo.exe
```

By default, that means the classic scene, 3840 x 2160 pixels, adaptive sampling
with a maximum of 1024 samples per pixel, and output to `hiprt_demo.bmp`.

All command examples below assume the repository root is the current directory.
Keep the executable together with the runtime files in `build\bin`.

## Command-line reference

```text
hiprt_demo.exe [OUTPUT.bmp|OUTPUT.png] [SAMPLES]
               [--scene classic|detail|FILE]
               [--fixed]
               [--mesh FILE.ply] [--mesh-up y|z]
```

| Argument / option | Meaning | If omitted |
| --- | --- | --- |
| `OUTPUT.bmp` or `OUTPUT.png` | Output filename. Its extension selects the image format, case-insensitively. | Uses the scene's `image.output`. |
| `SAMPLES` | Positive integer: maximum samples per pixel; with `--fixed`, the exact count per pixel. Requires an output filename before it. | Uses the scene's `sampling.max` or fixed `sampling.count`. |
| `--scene NAME_OR_FILE` | Selects preset `classic`, preset `detail`, or a `.hipscene` file. | Uses the classic scene. |
| `--fixed` | Disables adaptive sampling and renders the maximum count for every pixel. | Uses the scene's sampling mode. |
| `--mesh FILE.ply` | Replaces the scene's single cat or PLY mesh with this file. | Uses the scene's existing meshes. |
| `--mesh-up y` or `--mesh-up z` | Chooses the source mesh's up axis. Requires `--mesh`. | A `--mesh` replacement uses Y up; without a replacement, the scene's `up` setting applies. |

Options may appear before or after positional arguments, but **the output
filename must precede the sample count**. Quote paths containing spaces.
To change only the sample count, still supply an output filename:

```powershell
.\build\bin\hiprt_demo.exe preview.png 64
```

**There are currently no `--width`, `--height`, `--resolution`, `--samples`,
`--adaptive` or `--help` options.** Resolution and advanced settings are edited
in a scene file; the table above lists all supported command-line options.

### Common commands

```powershell
.\build\bin\hiprt_demo.exe preview.png 32
.\build\bin\hiprt_demo.exe reference.png 256 --fixed
.\build\bin\hiprt_demo.exe --scene .\scenes\my-scene.hipscene
.\build\bin\hiprt_demo.exe text.png 64 --scene .\scenes\text.hipscene
.\build\bin\hiprt_demo.exe model.png 64 --mesh "C:\models\my model.ply"
.\build\bin\hiprt_demo.exe detail.png 64 --scene detail --mesh "C:\models\Dragon-ply.ply" --mesh-up z
```

PNG is lossless and stores the same rendered pixels as BMP, usually in a much
smaller file. Only `.png` and `.bmp` are supported; missing or other extensions
are rejected before GPU setup. Existing output files are overwritten.

## Which settings can I change where?

| Setting | Command line | Scene file |
| --- | --- | --- |
| Output path / PNG or BMP | First positional argument | `image.output` |
| Resolution | **No option** | `image.size [width, height]` |
| Maximum samples per pixel | Second positional argument | Adaptive `max` or fixed `count` |
| Fixed sampling | `--fixed` | `sampling fixed` |
| Adaptive sampling | No option to enable it | `sampling adaptive` |
| Sampling batches / convergence thresholds | No options | Fields in the `sampling` block |
| Imported mesh / its up axis | `--mesh`, `--mesh-up` | Fields in each `mesh` block |
| Camera, room, lighting, materials, text and object placement | No options | Corresponding scene blocks |

Command-line values override only the settings explicitly supplied. Other
settings remain as defined in the selected scene.

## Changing the resolution

Resolution is controlled by the **`image` block in the scene file**, not by a
command-line option.

For example, make a copy of the classic scene:

```powershell
Copy-Item .\scenes\classic.hipscene .\scenes\preview.hipscene
```

In the copy, replace the existing `image` block with:

```text
image {
    size [960, 540]
    output "preview.png"
}
```

Then render it with a low sample budget:

```powershell
.\build\bin\hiprt_demo.exe preview.png 32 --scene .\scenes\preview.hipscene
```

This produces a **960 x 540 PNG with at most 32 samples per pixel**. Other useful
sizes are `[1280, 720]`, `[1920, 1080]` and `[3840, 2160]`.
Reducing either resolution or sample count reduces rendering work.

Copying a scene alongside the original preserves the base directory for its
relative asset paths. Moving it to another directory may require updating those
paths.

## Samples and image quality

More samples generally reduce noise but increase render time.

- **Adaptive:** each pixel may stop early when its estimated noise is low enough.
  The supplied sample count is an upper limit.
- **Fixed:** every pixel receives exactly the supplied sample count. Use
  `--fixed` for fixed-sample comparisons.

The classic and detail scenes use this adaptive configuration:

```text
sampling adaptive {
    min 64
    specular_min 256
    max 1024
    batch 8
    relative_error 0.05
    brightness_floor 0.05
}
```

| Field | Meaning |
| --- | --- |
| `min` | Minimum samples before ordinary pixels can stop early. |
| `specular_min` | Minimum for pixels whose primary ray hits glass, metal or Phong material. |
| `max` | Maximum samples for any pixel; overridden by the CLI sample count. |
| `batch` | Number of sample passes per GPU batch. Smaller batches give more frequent progress updates. |
| `relative_error` | Estimated relative RGB noise threshold. Lower values demand more samples. |
| `brightness_floor` | Lower bound on the brightness used when checking relative noise in dark pixels. |

The maximum always wins: requesting 32 samples caps rendering at 32 even when
`min` or `specular_min` is higher. The renderer reports actual minimum, average
and maximum sample counts after adaptive rendering. Adaptive stopping is a
heuristic, not a guarantee of matching fixed-sample quality.

For fixed sampling directly in a scene, replace the entire sampling block:

```text
sampling fixed {
    count 256
    batch 8
}
```

There is no CLI switch to re-enable adaptive sampling for a fixed-sampling scene.
Edit its sampling block instead.

## Supplied scenes and assets

| Scene | Selection | Default settings | External assets |
| --- | --- | --- | --- |
| [Classic](scenes/classic.hipscene) | `--scene classic` | 3840 x 2160, adaptive up to 1024 samples, `hiprt_demo.bmp` | None. |
| [Detail](scenes/detail.hipscene) | `--scene detail` | 3840 x 2160, adaptive up to 1024 samples, `detail.bmp` | PLY mesh and a font. |
| [Text](scenes/text.hipscene) | `--scene .\scenes\text.hipscene` | 1280 x 720, adaptive up to 256 samples, `text.bmp` | A font. |

**Classic** contains colored walls, a ceiling light, spheres, tilted tori, a
faceted crystal and a low-poly cat.

**Detail** contains a luminous torus, mirror and glass spheres, an imported mesh,
procedural tiles/wood/marble/fractals, and gold "HIP RT" lettering projecting out
of the back wall. The scene refers to a separately downloaded dragon mesh.
Either edit its mesh path or use `--mesh` to supply your own.

**Text** demonstrates extruded lettering with holes, curved outlines,
reflections and shadows. `text` is not a preset alias; pass its scene file path.

The detail and text scenes refer to Windows Arial. On other systems, change
their `font` paths to a local scalable TrueType or OpenType font. Fonts and
external model assets are not bundled; check their licenses before use.

### Importing a PLY mesh

`--mesh` requires a scene with **exactly one cat or PLY mesh**. It replaces that
object, not the whole scene. Zero or multiple replacement candidates are errors;
use scene-file `mesh` blocks to configure multiple models independently.

Meshes are uniformly scaled to fit the configured space, horizontally centered,
and positioned with their bottom at the configured Y coordinate. Replacing the
classic cat uses its original space and a diffuse copper-colored material.
Replacing a scene's PLY mesh preserves that mesh's fitting, position and material.

Use `--mesh-up z` for Z-up source models; the default for CLI replacements is Y up.
PLY files may be ASCII or little-/big-endian binary, but **faces must already be
triangles**. Vertex colors, normals and texture coordinates are ignored.

## Editing scene files

Start with a supplied `.hipscene` file and modify it. Sections must remain in
this order:

```text
hipscene 1
image { ... }
sampling adaptive { ... }
camera { ... }
room { ... }
material definitions
one light
objects
```

This is an outline, not a complete scene. Field order within blocks also matters;
use the supplied files as examples. Comments are not supported. Strings support
`\"` and `\\` escapes; Windows paths inside scene strings need escaped backslashes
or forward slashes.

Scenes control camera position/target/view size, room dimensions and surfaces,
named diffuse/metal/glass/Phong materials, one rectangle or torus light, and any
number of spheres, tori, PLY meshes and text objects. `cat` and `crystal` insert
the supplied procedural models.

For text, `value` is single-line UTF-8, `size` is the em size in scene units,
`depth` is extrusion depth, and `position` is the baseline origin at the back cap.
Text lies in XY and extrudes toward +Z. Complex-script shaping, ligatures and
bidirectional layout are not supported.

### Path rules

| Path | Relative to |
| --- | --- |
| `--scene FILE` | Current working directory. |
| `--mesh FILE.ply` | Current working directory. |
| Output path, whether on the CLI or in a scene | Current working directory. |
| `file` in a scene's `mesh` block | The scene file's directory. |
| `font` in a scene's `text` block | The scene file's directory. |

The `classic` and `detail` aliases locate the source-tree scene directory
recorded when building, independently of the working directory. Keep those
scene files available, or select another file explicitly.

## Progress and troubleshooting

- **Progress:** `Rendering: 0% ... 100%` counts completed sample passes against
  the maximum budget. It is not a remaining-time estimate or the percentage of
  converged pixels.
- **100% but no image yet:** GPU rendering has finished; downloading the image
  and writing/compressing it happen afterward, with a separate status message.
- **Low CPU utilization:** normal while waiting for the GPU. Large meshes also
  need setup time and GPU memory before rendering starts.
- **Cannot load a mesh or font:** check the [path rules](#path-rules), asset
  availability and the font paths in the selected scene.
- **Unknown option:** use the [CLI reference](#command-line-reference).
  Resolution is set in the scene, and sample count is positional.
- **Scene syntax error:** errors include the scene filename, line and column.
  Check section/field order and material names.

Reported GPU time excludes scene setup, image download and PNG/BMP writing.

## Requirements

- A compatible AMD GPU.
- AMD HIP SDK **7.1**, including HIP RT **3.0**, its device library and runtime bundles.
- A C++23-capable host compiler and CMake **3.25+**.
- Boost **1.92+** headers.
- Git and internet access on first configure for FreeType, libpng and zlib.

On Windows, install the Visual Studio C++ build tools. CMake defaults to HIP SDK
at `C:\Program Files\AMD\ROCm\7.1` and Boost at
`%USERPROFILE%\Downloads\boost_1_92_0`. Different HIP/HIP RT versions are rejected.

Optional **CMake settings** (not renderer command-line options):

| Setting | Purpose |
| --- | --- |
| `-DBoost_ROOT=C:\path\to\boost` | Use another Boost installation. |
| `-DHIPCC_EXECUTABLE=C:\path\to\hipcc.exe` | Select the HIP SDK 7.1 compiler. |
| `-DHIP_ARCHITECTURE=gfx1201` | Build for an explicit GPU architecture instead of detecting local GPUs. Separate multiple targets with semicolons. |

Quote a complete `-D...` argument when its path contains spaces. Reconfigure
after changing GPUs; at runtime the app automatically prefers a compatible
discrete GPU over an integrated one.

FreeType, libpng and zlib are built statically from pinned sources. For offline
builds, set `FETCHCONTENT_SOURCE_DIR_FREETYPE`, `FETCHCONTENT_SOURCE_DIR_LIBPNG`
and `FETCHCONTENT_SOURCE_DIR_ZLIB` to local source directories.

On Linux/WSL, use the same CMake build commands with SDK 7.1 and a compatible
GPU environment; run `./build/bin/hiprt_demo` instead of the Windows executable.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Tests cover scene loading, meshes, text, materials, rendering progress and
PNG/BMP output. GPU tests require a compatible GPU. Set the CMake variable
`HIP_DEMO_TEST_FONT` to a scalable font file if Arial or DejaVu Sans is not found.

## Distribution and licenses

Keep the HIP/HIP RT runtime files copied to `build\bin` alongside the executable.
Include the copied `RPLY-LICENSE.txt`, `FREETYPE-LICENSE.txt`,
`LIBPNG-LICENSE.txt`, `ZLIB-LICENSE.txt` and `BOOST-MATH-LICENSE.txt`
when redistributing binaries.

Sphere, torus and text intersections use analytic single-precision polynomial
solvers adapted from Boost.Math, with bounded root polishing rather than
iterative root searches. The adaptation is covered by the
[Boost Software License](third_party/boost_math/LICENSE_1_0.txt).

PLY support uses RPly 1.1.3 by Diego Nehab under the MIT license; its source and
notices are in [third_party/rply](third_party/rply).

Portions of this software are copyright (c) 2025 The FreeType Project
(https://freetype.org). All rights reserved. FreeType is used under the
FreeType License.
