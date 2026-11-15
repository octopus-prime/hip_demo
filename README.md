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

For rendered examples and syntax for every geometry, material and texture, see
the [visual reference](docs/visual-reference.md).

| Scene | Selection | External assets |
| --- | --- | --- |
| [Classic](scenes/classic.hipscene) | `--scene classic` | None. |
| [Detail](scenes/detail.hipscene) | `--scene detail` | PLY mesh and a font. |
| [Text](scenes/text.hipscene) | `--scene .\scenes\text.hipscene` | A font. |
| [CSG](scenes/csg.hipscene) | `--scene .\scenes\csg.hipscene` | None. |
| [CSG text](scenes/csg_text.hipscene) | `--scene .\scenes\csg_text.hipscene` | A font. |
| [Mesh light](scenes/mesh_light.hipscene) | `--scene .\scenes\mesh_light.hipscene` | None; original panel mesh included. |
| [Dragon light](scenes/dragon_light.hipscene) | `--scene .\scenes\dragon_light.hipscene` | Same dragon PLY mesh as Detail. |
| [Quadrics](scenes/quadrics.hipscene) | `--scene .\scenes\quadrics.hipscene` | None. |
| [CSG quadrics](scenes/csg_quadrics.hipscene) | `--scene .\scenes\csg_quadrics.hipscene` | None. |
| [Instancing](scenes/instancing.hipscene) | `--scene .\scenes\instancing.hipscene` | Dragon PLY mesh and Arial font. |

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

Triangle meshes use HIP RT's high-quality BVH builder, favoring traversal speed
over the fastest scene preparation. Triangle pairing remains disabled with the
current unindexed, expanded triangle buffer.

## Editing scene files

Start with a supplied `.hipscene` file. Sections remain in this order:

```text
hipscene 1
camera { ... }
room { ... }
material definitions
optional legacy light block
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
named diffuse/metal/glass/Phong/emissive materials, and any
number of spheres, tori, rectangles, CSG solids, PLY meshes and text objects. `cat` and `crystal` insert
the supplied procedural models.
Ellipsoids, cylinders, cones, paraboloids and hyperboloids are also supported as
ordinary objects.

### Quadrics

The [quadrics demo](scenes/quadrics.hipscene) shows, left to right: a sphere,
ellipsoid, cylinder, cone, paraboloid and hyperboloid. All six have distinct CPU
components and lower to **one GPU Quadric model**, without triangulation.

```text
sphere { center [-5, 1, 0] radius 1 material gold }
ellipsoid { center [-3, 1, 0] radii [0.7, 1, 0.5] material blue }
cylinder { center [-1, 1, 0] radius 0.65 height 2 material green }
cone { center [1, 1, 0] radius 0.75 height 2 material copper }
paraboloid { center [3, 1, 0] radius 0.8 height 2 material purple }
hyperboloid { center [5, 1, 0] radius 0.4 axial_radius 0.75 height 2 material cyan }
```

Define the referenced materials before these objects. All radii and heights must
be finite and positive. Ellipsoid `radii` are the X/Y/Z semiaxes. The other new
forms are aligned with Y; `center` is the midpoint of their height interval.
Their lower and upper limits are `center.y - height/2` and `center.y + height/2`:

- **Cylinder:** constant radius, with bottom and top disks.
- **Cone:** `radius` at the bottom, narrowing to a point at the top; closed base.
- **Paraboloid:** point at the bottom, widening to `radius` at the closed top.
  Squared cross-section radius grows linearly with height.
- **Hyperboloid:** a one-sheet, hourglass-shaped body with both end disks.
  `radius` is the waist radius; `axial_radius` controls flaring.
  At height offset `y`, its cross-section radius is
  `radius * sqrt(1 + (y/axial_radius)^2)`.

The GPU stores a normalized local-space quadratic polynomial, its center/scale
and height-capping flag. Bounds are finite and enclose the complete capped body;
they do not substitute for the actual cap intersections. Normals come from the
polynomial gradient transformed by inverse scale. Rays are recentered within the
bounds and their parameter is scaled before solving the quadratic equation.
Unusable FP32 dimensions or collapsed bounds are rejected during GPU preparation.
At the cone's singular apex, an axial cap hit supplies an axial normal; singular
side hits without a defined normal are ignored.

All six quadratic shapes can also be CSG operands. Like spheres, the new quadrics
may use emissive materials but currently have no direct-light surface sampler.

### Shared geometry instances

Declare prototypes after materials/the optional legacy light, before ordinary
objects. Place instances after ordinary objects:

```text
prototype ring = torus {
    center [0, 0, 0] major_radius 1 minor_radius 0.25 tilt_x 0 material gold
}
instance { prototype ring position [-2, 1.5, 0] }
instance {
    prototype ring
    position [2, 1.5, 0]
    rotation [30, 0, 45]
    scale [1, 0.7, 1.2]
    material blue
}
```

A prototype does **not** place an object. All ordinary geometry forms, including
PLY meshes, text, CSG, `cat` and `crystal`, can be prototypes. Names must be unique
and references must exist; nested instances are not supported. Unused prototypes
are not prepared or uploaded. Existing ordinary objects remain compatible.

Transform fields are optional, in the order shown. Defaults are zero translation/
rotation and unit scale. Rotations use degrees, applied X, then Y, then Z:
`world = translation + Rz * Ry * Rx * scale * prototype_point`. Scale components
must be finite and strictly positive. Transforms pivot around the prototype's
coordinate origin, **not** its bounding-box center; mesh fitting and prototype
geometry coordinates are applied first. The optional material replaces the whole
prototype material, including built-in cat detail materials.

HIP RT builds one geometry/BVH per used prototype and a shared instance BVH.
Repeated PLY placements share their vertex/material buffers; repeated text and
CSG placements share their compiled geometry. Emissive instances also share local
surface-sampling distributions. Nonuniform scaling adjusts normals and area PDFs;
rectangle/torus/mesh instances support direct-light sampling, while quadric/text/
CSG instances retain the unsampled-emission behavior described below.

Diffuse, Phong, metal and glass materials optionally accept a procedural texture
suffix after their usual parameters:

```text
material timber = phong [1, 1, 1] texture wood_detail
material polished = metal [1, 1, 1] roughness 0.1 texture marble_detail
```

All six texture forms from the visual reference are supported. Their palette
replaces the RGB color rather than multiplying it. Instance textures use prototype
coordinates, so patterns move/rotate/scale with each placement. Fractal textures
on objects use a unit-sized local domain; room fractal mapping is unchanged.

The [instancing showcase](scenes/instancing.hipscene) places 109 instances of
17 prototypes: every geometry/material/texture kind, rotated tori, nonuniform
scales, 12 shared-geometry lights and six dragons. It requires the same external,
CC BY 3.0 dragon asset as Detail and `C:\Windows\Fonts\arial.ttf`; the dragon is not
bundled. Its approximately 30 million placed triangles use approximately
5 million stored triangles. Startup reports sharing and triangle vertex-buffer
sizes separately from BVHs/materials.

From `build\bin`, benchmark the entire scene with your existing full configuration:

```powershell
.\hiprt_demo.exe --scene ..\..\scenes\instancing.hipscene --output .\instancing.png --config .\full.cfg
```

Instancing saves geometry storage and repeated BVH construction; it does not
promise constant rendering time as placements increase.
Scenes without instances select specialized render kernels without the added
instance traversal/sampling code.

Reference measurement on an AMD Radeon RX 9070 XT: 3840x2160, adaptive 64-1024
samples, specular minimum 256, relative error 0.05, brightness floor 0.05 and
batch size 8 took **239.588 s GPU time** at **790.2 average samples/pixel**
(242.5 s total). Shared triangle vertex buffers occupied **228.9 MiB**, versus
1373.4 MiB for duplicated placements, excluding BVHs/materials. This measures the
showcase workload, not a timing comparison against duplicated geometry.

### Emissive objects and mesh lights

Lights are ordinary objects with an emissive material. RGB values specify
finite, nonnegative **radiance**, not reflectance; values above 1 are allowed:

```text
material lamp = emissive [16, 10, 5]
mesh {
    file "light_panel.ply"
    up y
    fit [3, 1, 2]
    position [0, 4, 0]
    material lamp
}
rectangle { center [0, 5, 0] width 2 depth 1 material lamp }
```

Rectangles lie in XZ and face downward. Meshes need not be closed: their triangle
winding determines the emitting side. All emission is **one-sided**, including
camera and reflected hits. Each object emits its own material's radiance.
Multiple emitters and scenes without a legacy light block are supported.
Existing `light ceiling_rectangle` and `light torus` blocks remain compatible;
the builder translates them into rectangle/torus objects with emissive materials.
The room ceiling itself no longer contains a special light region.

Direct lighting samples rectangles, tori and meshes through references to the
prepared geometry, without a second light-shape model. Mesh triangles are selected
proportionally to their **prepared geometry area**, then sampled uniformly within
the triangle. Instances reuse this local distribution and adjust world-space
PDFs by the surface-area Jacobian. Degenerate triangles are skipped; wholly degenerate emissive meshes
are rejected. PDFs include emitter selection; tilted tori use their geometry's
transform and nonuniform angular-sampling PDF.

Quadric, text and CSG emission remains visible through camera/BSDF rays, but these
geometries do not yet have direct-light samplers and can converge more slowly.
Direct-light samples and diffuse/Phong BSDF hits on sampled emitters are combined
with **multiple importance sampling (MIS)** using the power heuristic. Both PDFs
use solid angle, including emitter selection and the area-to-solid-angle Jacobian.
Phong uses the full diffuse/specular mixture PDF and matching scattering weights.
This avoids double counting while allowing BSDF sampling to efficiently find
nearby emitting surfaces, such as the underside of the dragon's pedestal.
Camera hits, existing metal/glass transport and unsampled emitters retain full
emission contributions; the final bounce uses direct lighting without a competing
BSDF continuation. MIS reduces sampling variance, not geometry detail, and does
not clamp radiance or filter pixels. Difficult glass caustics can still converge
slowly.

Direct-light visibility uses distance-bounded HIP RT any-hit traversal without
material or texture shading, stopping as soon as a blocker is found. Room walls
also block these rays; glass remains opaque to this visibility query, as before.
The light endpoint tolerance, sample count and path estimator are unchanged.

Try the self-contained [mesh-light scene](scenes/mesh_light.hipscene), which combines
a tilted, unequal-triangle panel with an emissive torus.
The [dragon-light scene](scenes/dragon_light.hipscene) uses the Detail dragon as
the only emitter, with warm-orange radiance and mirror/glass spheres alongside.
Constant emission makes the dragon itself appear uniformly luminous rather
than diffusely shaded; its silhouette, occlusion and reflections reveal the shape.

For text, `value` is single-line UTF-8, `size` is the em size in scene units,
`depth` is extrusion depth, and `position` is the baseline origin at the back cap.
Text lies in XY and extrudes toward +Z. Complex-script shaping, ligatures and
bidirectional layout are not supported.

### CSG: union, intersection and difference

A `csg` object combines closed quadrics (sphere, ellipsoid, cylinder, cone,
paraboloid or hyperboloid), tori and extruded text without triangulation:

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
All leaf coordinates use the CSG object's coordinate system: scene coordinates
for ordinary CSG objects, local coordinates for CSG prototypes. Use an instance
to transform a complete CSG object.
Tori require `minor_radius < major_radius`; `tilt_x` is in degrees.

Set the material once on the `csg` object, not on its leaves. It also covers
new cut surfaces, whose normals point out of the remaining solid.
Meshes, rectangles and room surfaces cannot be CSG operands. A CSG object can
itself use an emissive material. The [CSG quadrics demo](scenes/csg_quadrics.hipscene)
shows nested unions and cutouts with all five new shapes. Quadric caps are part
of the solid and participate in Boolean cuts just like their curved sides.

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
analytic, single-precision quadric side/cap, torus and text cap/Bezier boundaries. There is no sphere tracing.
Tangencies and zero-volume contacts do not create solid intervals; identical
subtractions are empty. Nearly coincident boundaries are grouped within
`2 * float_epsilon * max(1, abs(ray_t))`, so sub-tolerance thin features can
disappear. At edges where multiple surfaces meet, the selected normal is
deterministic but not unique. Deep/large constructions cost more than simple
primitives; bounding boxes are not tightened for individual Boolean operations.
CSG evaluation caches analytic intersections and normals per ray, then advances
primitive parity, paired text contour winding, cap occupancy and Boolean states
without repeatedly solving the same polynomials; identical operands share their
cached events.
Cap membership normally uses those cached
crossings; ambiguous cap/contour edges retain the original point-fill tie-break.
The fast path uses local scratch for 64 events, 32 intersected glyphs and 64
leaf instructions. If any capacity is exceeded, the unrestricted boundary
streaming path is used instead: hits are never truncated and there is no
four-hit limit. Long strings and many overlapping operands can therefore
still cost substantially more than standalone text. Standalone text keeps its
existing intersection path.

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

## Scene model and GPU representation

The CPU scene uses standard graphics terminology:

- **Object** combines a **Geometry** and a **Material**.
- **Geometry** is a `std::variant` of sphere, ellipsoid, cylinder, cone,
  paraboloid, hyperboloid, torus, rectangle, mesh, extruded text and CSG.
  Geometry contains no material parameters.
- **Material** is a `std::variant` of diffuse, Phong, metal, glass and emissive
  descriptions. Each alternative supplies parameters for its shader/BSDF code.
- **Texture** supplies albedo or emitted radiance independently of the shader. Constant
  colors and the existing procedural patterns are texture alternatives; metal
  roughness remains a scalar.

For example, using the types in [scene/types.h](src/scene/types.h):

```cpp
using namespace hip_demo;
Object gold{
    Sphere{{0, 1, 0}, 0.5f},
    Metal{ConstantColor{{0.8f, 0.6f, 0.1f}}, 0.15f}
};
```

Geometry descriptions are value types, not a virtual class hierarchy. The six
quadratic CPU types lower to a shared GPU Quadric; torus retains its specialized
GPU model. Each type lives in its own
[component](src/components), including [Scene](src/components/scene) and
[Object](src/components/object):

Components are grouped by domain: [geometry](src/components/geometry) contains
the shape components and shared GPU quadric model; [material](src/components/material) contains
diffuse, Phong, metal, glass and emissive; [texture](src/components/texture)
contains the texture alternatives; [light](src/components/light) contains
legacy light-block parsing/building and rectangle/torus/mesh samplers. There is
no separate CPU light-shape model. Rectangles lower to two mesh triangles.
Domain composition files stay directly in the
domain folder, with each alternative's files in a child folder. Namespaces
mirror this hierarchy, for example `hip_demo::material::phong`.
Scene, Object, camera, room and shared value components remain at the root.

- `description.h`: owned AST data, with double-valued numeric parameters.
- `parser.h`: Boost.Parser rules and definitions, in the component's
  `parsing` namespace. [parser.cpp](src/parsing/parser.cpp) registers all rules
  with one `BOOST_PARSER_DEFINE_RULES`; rule IDs share its namespace for ADL.
- `builder.h`: semantic validation and conversion to the component model.
- `model.h`: renderer values and device method declarations. Larger algorithms
  live alongside them, such as `intersection.h`, `scattering.h` or `outline.cpp`.
- `gpu_model.h` / `preparation.h`, where needed: buffer-facing payloads and
  lowering of resource-owning values.

For example, [sphere](src/components/geometry/sphere) owns its description, both ordinary
and CSG grammar rules, validated CPU builder and GPU-Quadric builder.
`hip_demo::geometry::sphere::Model` is also available as `hip_demo::Sphere`; corresponding
aliases keep the existing C++ object-construction API usable.
Scene settings compose camera and room models and a typed light variant:
use `scene.settings.camera.position` and `scene.settings.room.textures`.
Scene and Object builders compose their child components rather than repeating
their validation. Components without scene-file syntax need no parser rule.

The pipeline is syntax AST -> validated CPU objects -> prepared GPU data.
[prepare_scene](src/components/scene/preparation.h) lowers variants to explicitly tagged,
trivially copyable [GPU payloads](src/gpu/types.h), compiles CSG trees into
Boolean jump programs, and prepares mesh/font geometry. Resource-owning CPU
variants are not copied to the device. The GPU geometry payload contains a
Quadric/Torus tagged union rather than separate GPU sphere and torus models.
[shader::visit](src/shader/visit.h) dispatches the existing GPU shader tag to
concrete component shader types with `evaluate` and `scatter` methods and
lighting traits. The integrator does not branch on individual shader IDs.
This is a device-compatible tagged visitor, not device `std::visit`: the current
Windows HIP/MSVC STL implementation reaches a host-only exception helper.
Invalid shader tags trap instead of silently selecting a default shader.
The visitor improves organization, not inherently dispatch cost or divergence.
Intersection code determines
geometry; traversal attaches the material and evaluates its textures before
shader/BSDF evaluation. Geometric normals remain separate from texture-modified
shading normals.

Existing scene syntax and CSG root-material behavior are unchanged.
Procedural material textures can be selected through the C++ scene model;
the scene-file grammar still exposes them only for room walls. Existing
procedural coordinate mappings are retained rather than adding a new UV API.
GPU object IDs retain their grouped order: primitives, CSG roots, standalone
text objects, then meshes; they are not indices into the CPU `objects` vector.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Tests cover CLI/config precedence and round-trips, scene loading, meshes, text,
materials, quadric sides/caps/apices/normals/intervals, rendering progress and
PNG/BMP output. GPU tests require a compatible
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
