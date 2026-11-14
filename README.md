# HIP RT Demo

A command-line GPU path tracer for AMD GPUs. It renders spheres, tori, CSG solids,
PLY meshes and extruded text, with reflections, glass, glossy materials and
procedural textures. Images are saved as **PNG or BMP**.

**Scene files describe the scene. CLI options and render configs control how
it is rendered.** Changing resolution or quality does not require editing a
scene.

## Quick start

From the repository root, with the [requirements](#requirements) installed:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\bin\hiprt_demo.exe --scene classic --output preview.png --width 960 --height 540 --samples 32
```

From `build\bin`, render the detail scene as PNG:

```powershell
.\hiprt_demo.exe --scene detail --output detail.png --samples 1024
```

Keep the executable together with the runtime files in `build\bin`.
Except where noted, examples below assume the repository root.

Running without arguments renders the classic scene at **3840 x 2160**,
using adaptive sampling with at most **1024 samples per pixel**, and writes
**`render.png`** in the current directory. These defaults are independent of
which scene is selected.

## Command-line reference

```text
hiprt_demo.exe [options]
```

| Option | Meaning | Default |
| --- | --- | --- |
| `--help`, `-h` | Show generated help and exit; no scene or GPU is loaded. | |
| `--scene NAME_OR_FILE` | Select `classic`, `detail`, or a `.hipscene` file. | `classic` |
| `--config FILE` | Read render defaults from an explicit config file. | No config is read automatically. |
| `--save-config FILE` | Save effective render options and exit without rendering. | |
| `--output FILE`, `-o FILE` | Output filename; `.png` or `.bmp` selects the format, case-insensitively. | `render.png` |
| `--width N` | Image width in pixels. | `3840` |
| `--height N` | Image height in pixels. | `2160` |
| `--sampling adaptive\|fixed` | Adaptive or fixed sampling. | `adaptive` |
| `--samples N` | Maximum samples per pixel; exact count with fixed sampling. | `1024` |
| `--min-samples N` | Minimum before ordinary pixels may converge. | `64` |
| `--specular-min-samples N` | Minimum for pixels whose primary ray hits glass, metal or Phong material. | `256` |
| `--batch-size N` | Sample passes per GPU batch; smaller batches give more frequent progress updates. | `8` |
| `--relative-error VALUE` | Adaptive relative RGB noise threshold; lower demands more samples. | `0.05` |
| `--brightness-floor VALUE` | Brightness floor when checking noise in dark pixels. | `0.05` |
| `--device N` | HIP device index; `-1` automatically prefers a compatible discrete GPU. | `-1` |

Quote CLI paths containing spaces. Unknown/abbreviated options, duplicate
options within one input, invalid values and positional arguments are rejected.
Dimensions and sample counts must be positive integers; both adaptive thresholds
must be finite and positive. Dimensions must fit the renderer and BMP limits.
An explicitly selected GPU must exist and match the compiled GPU architecture.

PNG is lossless and stores the same pixels as BMP, usually in a much smaller
file. Unsupported or missing image extensions are rejected before GPU setup.
Existing output images are overwritten.

### Common commands

```powershell
# Low-resolution detail preview
.\build\bin\hiprt_demo.exe --scene detail -o preview.png --width 960 --height 540 --samples 32

# Full-HD, exactly 256 samples per pixel
.\build\bin\hiprt_demo.exe --scene detail -o reference.png --width 1920 --height 1080 --sampling fixed --samples 256

# Text scene, with an explicit resolution and quality budget
.\build\bin\hiprt_demo.exe --scene .\scenes\text.hipscene -o text.png --width 1280 --height 720 --samples 256

# Custom scene on HIP device 0
.\build\bin\hiprt_demo.exe --scene .\scenes\my-scene.hipscene -o model.bmp --device 0
```

## Reusable render configs

Precedence, from lowest to highest:

**Built-in defaults < config file < explicit CLI options.**

Option order on the command line does not change precedence. A config contains
only render options, not the scene selection or control options such as
`config`, `help` and `save-config`.

Save your preferred effective settings:

```powershell
.\build\bin\hiprt_demo.exe --width 1920 --height 1080 --samples 256 --save-config quality.cfg
```

This writes the config and exits **without rendering, loading a scene or
initializing a GPU**. The resulting INI-style file contains every render option:

```ini
width = 1920
height = 1080
output = render.png
sampling = adaptive
samples = 256
min-samples = 64
specular-min-samples = 256
batch-size = 8
relative-error = 0.05
brightness-floor = 0.05
device = -1
```

Floating-point values may be written with extra digits to preserve their exact
value when reloaded. Scene selection is deliberately not saved, so the same
config can be reused with any scene:

```powershell
.\build\bin\hiprt_demo.exe --config quality.cfg --scene detail -o detail.png
.\build\bin\hiprt_demo.exe --config quality.cfg --scene classic -o classic.png --samples 64

# Save a modified copy of a config, again without rendering
.\build\bin\hiprt_demo.exe --config quality.cfg --samples 512 --save-config final.cfg
```

Configs are read using Boost.Program_options. Write values **without quotes**;
spaces inside filenames are preserved. `#` starts a comment, and surrounding
whitespace is stripped. Saving a config rejects output filenames containing `#`,
line breaks or surrounding whitespace rather than silently changing the path.
Existing config files are overwritten only when explicitly selected with
`--save-config`. Missing/unreadable configs and write failures are errors.

## Samples and image quality

More samples generally reduce noise but increase render time.

- **Adaptive:** pixels may stop early when estimated noise is low enough.
  `--samples` is an upper limit.
- **Fixed:** every pixel receives exactly `--samples` samples. Select
  `--sampling fixed` for fixed-sample comparisons.

The maximum always wins: `--samples 32` caps every pixel at 32 even when either
adaptive minimum is higher. The renderer reports actual minimum, average and
maximum sample counts after adaptive rendering. Adaptive stopping is a heuristic,
not a guarantee of matching fixed-sample quality.

## Supplied scenes and assets

| Scene | Selection | External assets |
| --- | --- | --- |
| [Classic](scenes/classic.hipscene) | `--scene classic` | None. |
| [Detail](scenes/detail.hipscene) | `--scene detail` | PLY mesh and a font. |
| [Text](scenes/text.hipscene) | `--scene .\scenes\text.hipscene` | A font. |
| [CSG](scenes/csg.hipscene) | `--scene .\scenes\csg.hipscene` | None. |
| [CSG text](scenes/csg_text.hipscene) | `--scene .\scenes\csg_text.hipscene` | A font. |

Classic contains colored walls, a ceiling light, spheres, tilted tori, a faceted
crystal and a low-poly cat. Detail contains a luminous torus, mirror and glass
spheres, an imported dragon, procedural room surfaces, and gold "HIP RT" lettering
projecting out of the back wall. Text demonstrates extruded curved lettering
with holes, reflections and shadows. CSG shows a gold union of spheres (left),
a blue intersection/lens (center), a copper sphere with a spherical cutout
(right), and a green nested torus/sphere construction (back). Text and CSG
are selected by file path, not preset aliases.
CSG text demonstrates raised lettering joined to a sphere (left), lettering
clipped by a sphere (center), and engraved lettering subtracted from a sphere
(right).

From `build\bin`, render the CSG demo to PNG:

```powershell
.\hiprt_demo.exe --scene ..\..\scenes\csg.hipscene --output csg.png --width 1920 --height 1080 --samples 256
```

Detail refers to a separately downloaded dragon mesh. Edit its scene `mesh.file`
to use your own asset. Detail and text refer to Windows Arial; on other systems,
change their `font` paths to a local scalable TrueType or OpenType font. Fonts
and external models are not bundled; check their licenses before use.
The CSG text demo also uses Windows Arial.

### Importing a PLY mesh

Configure each model in a scene `mesh` block: `file`, `up y` or `up z`, `fit`,
`position` and `material`. Meshes are uniformly scaled to fit the specified space,
horizontally centered, and placed with their bottom at the configured Y position.

PLY files may be ASCII or little-/big-endian binary, but **faces must already be
triangles**. Vertex colors, normals and texture coordinates are ignored.

## Editing scene files

Start with a supplied `.hipscene` file. Sections remain in this order:

```text
hipscene 1
camera { ... }
room { ... }
material definitions
one light
objects
```

This is an outline, not a complete scene. Field order matters; use the supplied
files as examples. Comments are not supported. Strings support `\"` and `\\`
escapes; Windows paths inside scene strings need escaped backslashes.
Whitespace around punctuation is optional (`csg{` and `csg {` are equivalent);
words must retain their token boundaries (`catcrystal` is not two objects).
Syntax is parsed into an AST first. Value ranges, material references and other
semantic constraints are checked when building the renderer's scene model;
errors identify the source file, line and column of the relevant scene block.

Scenes control camera position/target/view size, room dimensions and surfaces,
named diffuse/metal/glass/Phong materials, one rectangle or torus light, and any
number of spheres, tori, CSG solids, PLY meshes and text objects. `cat` and `crystal` insert
the supplied procedural models.

For text, `value` is single-line UTF-8, `size` is the em size in scene units,
`depth` is extrusion depth, and `position` is the baseline origin at the back cap.
Text lies in XY and extrudes toward +Z. Complex-script shaping, ligatures and
bidirectional layout are not supported.

### CSG: union, intersection and difference

A `csg` object combines closed spheres, tori and extruded text without triangulation:

```text
csg {
    material gold
    difference {
        union {
            sphere { center [0, 1, 0] radius 1 }
            torus { center [0, 1, 0] major_radius 1 minor_radius 0.25 tilt_x 90 }
        }
        sphere { center [0, 1.4, 0.8] radius 0.7 }
    }
}
```

Define `gold` as a named material earlier in the scene. Each `union`,
`intersection` or `difference` takes **exactly two operands**, which may themselves
be nested operations. `difference` subtracts the second operand from the first.
All leaf coordinates are scene coordinates; there is no separate group transform.
Tori require `minor_radius < major_radius`; `tilt_x` is in degrees.

Set the material once on the `csg` object, not on its leaves. It also covers
new cut surfaces, whose normals point out of the remaining solid.
Meshes, room surfaces and lights cannot be CSG operands.

Text operands use the same font, value, size, depth and position fields as
standalone text, but **omit the leaf material**:

```text
csg {
    material copper
    difference {
        sphere { center [0, 1.2, 0] radius 1.2 }
        text {
            font "C:\\Windows\\Fonts\\arial.ttf"
            value "CSG"
            size 1
            depth 1
            position [-1.05, 0.85, 0.55]
        }
    }
}
```

All glyphs form one solid operand: their volumes are united, overlapping
glyphs do not cancel, and letter counters remain open. Whitespace-only operands
are empty. Text still lies in XY and extrudes toward +Z, with the font path
relative to the scene file. Operand glyphs are not rendered separately.

From `build\bin`, start with a small preview:

```powershell
.\hiprt_demo.exe --scene ..\..\scenes\csg_text.hipscene --output csg-text.png --width 960 --height 540 --samples 128
```

HIP RT traverses conservative bounding boxes; custom GPU intersections combine
analytic, single-precision sphere/torus and text cap/Bezier boundaries. There is no sphere tracing.
Tangencies and zero-volume contacts do not create solid intervals; identical
subtractions are empty. Nearly coincident boundaries are grouped within
`2 * float_epsilon * max(1, abs(ray_t))`, so sub-tolerance thin features can
disappear. At edges where multiple surfaces meet, the selected normal is
deterministic but not unique. Deep/large constructions cost more than simple
primitives; bounding boxes are not tightened for individual Boolean operations.
Text CSG streams boundaries rather than imposing a four-hit limit. It also
evaluates contour winding to determine solid membership, so long strings and
many overlapping operands can cost substantially more than standalone text.

### Migration from the previous CLI and scene format

- Remove `image` and `sampling` blocks from custom scenes; they are now rejected.
  Transfer their values to CLI options or a render config.
- Replace positional `output.png 64` with `--output output.png --samples 64`.
- Replace `--fixed` with `--sampling fixed`.
- `--mesh` and `--mesh-up` are removed. Set the mesh file and up axis in the scene.
- Resolution, sampling and output no longer vary implicitly with the scene.
  The global default output is now `render.png`.

### Path rules

| Path | Relative to |
| --- | --- |
| `--scene FILE`, `--config FILE`, `--save-config FILE` | Current working directory. |
| Output path, on the CLI or in a render config | Current working directory, not the config directory. |
| `file` in a scene's `mesh` block | The scene file's directory. |
| `font` in a scene's `text` block | The scene file's directory. |

The `classic` and `detail` aliases locate the source-tree scene directory
recorded when building, independently of the working directory. Keep those
scene files available, or select another file explicitly.

## Progress and troubleshooting

- **Progress:** `Rendering: 0% ... 100%` counts completed sample passes against
  the maximum budget, not remaining time or the percentage of converged pixels.
- **100% but no image yet:** GPU work has finished; downloading and writing the
  image happen afterward, with a separate status message.
- **Low CPU utilization:** normal while waiting for the GPU. Large meshes also
  need setup time and GPU memory before rendering starts.
- **Cannot load a mesh or font:** check asset availability and the path rules.
- **Unknown option:** use `--help`. Old positional arguments and `--fixed` are
  no longer supported.
- **Scene syntax error:** errors include filename, line and column. Check
  section/field order, material names and obsolete render-setting blocks.

Reported GPU time excludes scene setup, image download and PNG/BMP writing.

## Requirements

- A compatible AMD GPU.
- AMD HIP SDK **7.1**, including HIP RT **3.0**, device library and runtime bundles.
- A C++23-capable host compiler and CMake **3.25+**.
- A full Boost **1.92+ source distribution**, including `libs/program_options/src`.
  CMake builds Program_options statically from these sources; a separate Boost
  bootstrap/build is not required.
- Git and internet access on first configure for FreeType, libpng and zlib.

On Windows, install the Visual Studio C++ build tools. CMake defaults to HIP SDK
at `C:\Program Files\AMD\ROCm\7.1` and Boost at
`%USERPROFILE%\Downloads\boost_1_92_0`. Different HIP/HIP RT versions are rejected.

Optional **CMake settings**, not renderer options:

| Setting | Purpose |
| --- | --- |
| `-DBoost_ROOT=C:\path\to\boost` | Use another full Boost source distribution. |
| `-DHIPCC_EXECUTABLE=C:\path\to\hipcc.exe` | Select the HIP SDK 7.1 compiler. |
| `-DHIP_ARCHITECTURE=gfx1201` | Build for an explicit architecture; separate multiple targets with semicolons. |

Quote a complete `-D...` argument when its path contains spaces. Reconfigure
after changing GPUs. At runtime, automatic device selection prefers a
compatible discrete GPU; `--device` selects an explicit index.

FreeType, libpng and zlib are built statically from pinned sources. For offline
builds, set `FETCHCONTENT_SOURCE_DIR_FREETYPE`, `FETCHCONTENT_SOURCE_DIR_LIBPNG`
and `FETCHCONTENT_SOURCE_DIR_ZLIB` to local source directories.

On Linux/WSL, use the same build commands with SDK 7.1 and a compatible GPU
environment; run `./build/bin/hiprt_demo` instead of the Windows executable.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Tests cover CLI/config precedence and round-trips, scene loading, meshes, text,
materials, rendering progress and PNG/BMP output. GPU tests require a compatible
GPU. Set `HIP_DEMO_TEST_FONT` to a scalable font if Arial or DejaVu Sans is absent.

## Distribution and licenses

Keep the HIP/HIP RT runtime files copied to `build\bin` alongside the executable.
Include the copied `RPLY-LICENSE.txt`, `FREETYPE-LICENSE.txt`,
`LIBPNG-LICENSE.txt`, `ZLIB-LICENSE.txt`, `BOOST-MATH-LICENSE.txt` and
`BOOST-PROGRAM-OPTIONS-LICENSE.txt` when redistributing binaries.

CLI/config parsing uses Boost.Program_options. Sphere, torus and text
intersections use analytic single-precision polynomial solvers adapted from
Boost.Math, with bounded root polishing rather than iterative root searches.
Both are covered by the [Boost Software License](third_party/boost_math/LICENSE_1_0.txt).

PLY support uses RPly 1.1.3 by Diego Nehab under the MIT license; source and
notices are in [third_party/rply](third_party/rply).

Portions of this software are copyright (c) 2025 The FreeType Project
(https://freetype.org). All rights reserved. FreeType is used under the
FreeType License.
