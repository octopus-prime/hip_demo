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

## HIP RT tutorial

`src2` is a separate, minimal starting point for learning HIP RT, independent
of the rendering pipeline but reusing its mesh import and polynomial helpers.
It selects HIP device 0, prints its name, architecture
and HIP RT header version, then uploads a diamond mesh (an octahedron with
eight triangles), a twelve-triangle box, an analytic unit sphere and a
configurable analytic torus, and a PLY dragon mesh. By default, it creates
**6,000,000 instances**: 100 x 100 x 75 spatial groups, each containing three
spheres, one diamond, two dragons, one box and one torus with different
materials and transforms. All instances
of each shape share its geometry and GPU buffers.
Each group uses six colored Phong materials, one perfect mirror and one clear
glass material (IOR 1.5). Their assignment rotates by one slot between
neighboring groups along each axis: prototype `i` in group `(x, y, z)` uses
material `(i + x + y + z) % 8`. Every shape therefore appears with every
material, including mirror and glass dragons. This is a static spatial
variation, not an animation; the executable still renders just one frame.
Use `--small` for just eight instances: three spheres and a torus above,
one diamond, two dragons and a box below. In both modes there are exactly five geometry
BVHs and one scene BVH.
That is six BVHs total, not one geometry BVH per instance.
A 2D grid of threads renders `scene.png` at 3840 x 2160 pixels (4K UHD),
including in `--small` mode.
Use `--dragon` for a separate scene with a glass sphere in front of one dragon
above a mirror floor, point lighting and a camera framing the objects and their reflections, also
at 4K. It writes `dragon.png`, leaving the other scenes' `scene.png` untouched,
and builds only the PLY geometry, the two-triangle floor and an analytic sphere.
The instances have base colors, Lambert diffuse lighting and optional Phong
highlights from a warm point light. Shadows remain enabled; there is no
additional ground or backdrop instance in the group scenes.
Ray tracing remains the default. `--integrator path` enables diffuse indirect
lighting and stochastic mirror/glass/diffuse path continuation using the same
scene buffers and BVHs. `--cornell` supplies a small open-front room with red
and green walls, a matte sphere and a glass sphere to demonstrate color bleeding;
an emissive ceiling rectangle supplies the light instead of a point source.
It needs no external PLY asset. Scene selectors are mutually exclusive.

Thin C++ types separate resource ownership from camera values:

- `Context` (`src2/context.h` / `.cpp`) creates and destroys the HIP RT context
  and assigns custom-intersection dispatch slots.
- `FunctionTable` (`src2/function_table.h`) owns a HIP RT function table.
  Each scene owns one for both closest-hit and shadow queries; standalone
  geometry queries create their own.
- `DeviceBuffer<T>` (`src2/device_buffer.h`) allocates GPU memory, uploads CPU
  data, downloads results and frees the allocation automatically. Its template
  implementation stays in the header.
- `load_ply_vertices(path, up)` (`src2/mesh.h` / `.cpp`) adapts the existing demo
  PLY reader and fitting helper to the tutorial's `std::vector<float3>`.
  It converts Z-up to Y-up by default (`MeshUp::y` preserves Y-up input),
  uniformly fits the mesh into a 2 x 2 x 2 box and centers its bounding box
  on the local origin. Triangle indices are expanded into consecutive vertices;
  winding is preserved. ASCII and little-/big-endian binary PLY files are
  supported, with triangle faces only. Vertex normals, colors and UVs are ignored.
- `Geometry` (`src2/geometry.h` / `.cpp`) owns GPU triangle vertices or custom
  bounds/payloads and a HIP RT geometry. Its mesh constructor accepts a nonempty
  `std::span<const float3>` containing consecutive groups of three finite
  vertices, one group per triangle (no index buffer).
  `Geometry(context, UnitSphere{})` describes a
  radius-1 sphere at the local origin. `Geometry(context, UnitBox{})` creates
  a mesh box with local bounds [-1, 1] on all axes.
  `Geometry(context, UnitPlane{})` creates a finite 2 x 2 rectangle at local
  Y = 0, with X/Z bounds [-1, 1] and normal +Y. It uses two triangles and
  HIP RT's built-in intersection, not an infinite analytic plane. Instances
  can scale it into floors or rotate it into walls; the dragon's mirror
  floor uses this same primitive. `Geometry(context,
  Torus{.major_radius = 0.8f, .minor_radius = 0.25f})` creates a ring torus
  around local Y: major radius is the distance from the origin to the tube
  centerline, minor radius is the tube radius. Radii must be finite with
  `0 < minor < major`, with representable float bounds/squares; horn and
  spindle tori are not supported. `build()` allocates temporary memory, builds the BVH and
  waits for the GPU before releasing that memory. `trace(ray)` launches the
  kernel, waits, downloads its result and returns `std::optional<hiprtHit>`:
  a hit or `std::nullopt` for a miss. Calling it before `build()` is an error.
- `Scene` (`src2/scene.h` / `.cpp`) owns a HIP RT scene and its GPU instance
  and transform buffers. Each `Instance` references an already-built
  `Geometry` and supplies translation, axis-angle rotation, scale and a
  `SurfaceMaterial material`. The material value is validated and equal values
  are uploaded once into a shared GPU palette. Each instance stores a 32-bit
  material ID; shading looks up `materials[material_ids[hit.instanceID]]`.
  Equality compares all eight float components exactly (signed zero compares equal),
  without approximate merging. The CPU `Instance` interface is unchanged.
  The material has an RGB
  `base_color` in [0, 1] (default 0.8 per channel).
  `Scene::build()` builds the top-level
  BVH; `Scene::trace()` returns a hit including `instanceID` and `primID`.
  Two instances can have different instance IDs but share primitive 0 of
  the same geometry. Transforms use HIP RT's `hiprtFrameSRT`; the tutorial
  does not implement its own matrix or intersection transformations.
  `specular_strength` in [0, 1] controls a white Phong highlight (default 0,
  retaining diffuse-only behavior); positive `shininess` controls its width
  (default 32). Higher exponents give narrower highlights.
  `reflectivity` in [0, 1] mixes local lighting with an ideal mirror reflection
  (default 0, retaining the existing images). This is separate from the
  Phong highlight; 1 selects a pure mirror.
  `transmission` in [0, 1] selects how much of the non-mirror part is glass
  (default 0); `ior` is a finite refractive index of at least 1 (default 1.5).
  Full transmission disables local diffuse/Phong shading and splits light
  between dielectric reflection and refraction using Fresnel.
  An `Instance` can also supply RGB `emission` (default zero). Nonzero emission
  must be finite and nonnegative; values above one are allowed. Currently only
  `UnitPlane` instances may emit. Emission is independent of the shared
  reflectance palette, so existing material IDs, palette sizes and GPU material
  layouts do not change. Emitters terminate paths and emit only from local +Y,
  transformed with their instance; their back face is opaque and black.
- `Camera` (`src2/camera.h` / `.cpp`) takes position, target, an up direction
  and vertical field of view in degrees. It validates the inputs and computes
  an orthonormal forward/right/up basis on the CPU. `GpuCamera` is its small
  value representation; `generate_ray()` runs on the GPU and generates a
  normalized perspective ray for a supplied subpixel offset (pixel center
  by default). Camera arithmetic uses
  `float` throughout and reuses `hiprt::cross`, `hiprt::dot` and
  `hiprt::normalize` from `hiprt/hiprt_math.h`.
- `Renderer` (`src2/renderer.h` / `.cpp`) owns the CPU and GPU pixel buffers.
  `Renderer(width, height, max_depth = 3, samples_per_pixel = 1,
  integrator = Integrator::ray)` accepts
  a secondary-ray depth limit in [0, 8] and a positive sample count. Both
  reflections, refractions and diffuse path bounces consume a depth step. The default permits
  up to four surface hits per branch and uses one pixel-center camera ray.
  `render(scene, camera, light)` launches 16 x 16 thread blocks and downloads the RGB
  results, returning `RenderTimings` (GPU, download and total milliseconds);
  `save(path)` writes a lossless RGB PNG using the existing
  libpng dependency.
  Saving before a successful render is an error. Repeated renders reuse the
  buffers; a failed render invalidates the previous image.
  Two reusable RAII `GpuEvent` objects time the kernel on the GPU.
- Internally, `GpuRenderer` contains the scene handle, GPU pixel
  pointer, dimensions and a `GpuCamera` value. Its `__device__` method `render_pixel()` does the
  actual work. The small `__global__` function `render_kernel()` is just the
  entry point that delegates each thread to that method.
- `DirectionalLight` (declared alongside `Renderer`) normalizes a finite,
  nonzero direction **toward** the light. The light is white with unit strength;
  omitting it from `render()` uses direction +Z.
- `PointLight` takes a finite position, RGB color in [0, 1] (default white)
  and finite nonnegative intensity (default 1). Its illumination is
  `color * intensity / distance_squared`; intensity 1 gives unit illumination
  at distance 1. Both light types use the same Lambert/Phong shading.

```cpp
Geometry diamond(context, vertices); // The eight mesh triangles defined in src2/main.cpp.
diamond.build();
Geometry sphere(context, UnitSphere{});
sphere.build();
Geometry box(context, UnitBox{});
box.build();
Geometry torus(context, Torus{.major_radius = 0.8f, .minor_radius = 0.25f});
torus.build();
Geometry dragon(context, load_ply_vertices("ply/Dragon-ply/Dragon-ply.ply"));
dragon.build();
const std::array instances{
    Instance{.geometry = sphere, .translation = {-3.9f, 1.25f, 0}},
    Instance{.geometry = sphere, .translation = {-1.3f, 1.25f, 0}},
    Instance{.geometry = sphere, .translation = {1.3f, 1.25f, 0}},
    Instance{.geometry = torus, .translation = {3.9f, 1.25f, 0},
             .rotation_axis = {1, 0, 0}, .rotation_degrees = 65},
    Instance{.geometry = diamond, .translation = {-3.9f, -1.25f, 0}},
    Instance{.geometry = dragon, .translation = {-1.3f, -1.25f, 0},
             .rotation_axis = {0, 1, 0}, .rotation_degrees = -20},
    Instance{.geometry = dragon, .translation = {1.3f, -1.25f, 0},
             .rotation_axis = {0, 1, 0}, .rotation_degrees = -45,
             .scale = {1.1f, 0.8f, 1}},
    Instance{.geometry = box, .translation = {3.9f, -1.25f, 0},
             .rotation_axis = {0.5f, 1, 0}, .rotation_degrees = 30,
             .scale = {0.75f, 0.75f, 0.75f}},
};
Scene scene(context, instances);
scene.build();
const Camera camera({0, 0, 10}, {0, 0, 0}, {0, 1, 0}, 42.0f);
Renderer renderer(3840, 2160);
const PointLight light({-1.0f, 4.0f, 6.0f}, {1.0f, 0.95f, 0.85f}, 65.0f);
renderer.render(scene, camera, light);
renderer.save("scene.png");
```

`Scene` borrows geometries: keep them alive and unchanged until the scene is
destroyed, and create them with the same `Context`. The CPU `Instance` array
only needs to live through the scene constructor, which uploads its data.
Empty scenes, non-finite translations, unbuilt geometries and geometries
from another context are rejected.
Scale components must be finite and positive; zero and negative scales are
not supported in this tutorial step. Rotation angles are finite degrees
(negative angles and full turns are allowed), converted to radians for
HIP RT. The axis is normalized with `hiprt::normalize`; its squared length
must be finite and nonzero in `float`, even for a zero-angle rotation.
The default axis is +Z, angle is zero, scale is (1, 1, 1), and translation
is zero, so existing translation-only instances retain their behavior.

Transform order is **scale, rotate, translate**:
`world = translation + rotation(scale * local)`, or `T * R * S` with column
vectors. Scaling and rotation use the geometry's local origin as pivot,
not the triangle's centroid. Positive angles follow the right-hand rule.
`Context` must outlive `Geometry`, `Scene` and `Renderer`; declaring it first in `main` ensures reverse
destruction order. Destructors release resources automatically (RAII), including
during exception unwinding, and report cleanup errors without throwing.
`Renderer` uses its members' destructors to release its buffers automatically;
the CPU object itself is never copied to the GPU.

```powershell
cmake -S . -B build
cmake --build build --config Release --target hiprt_tutorial
.\build\bin\hiprt_tutorial.exe
# Small scene:
.\build\bin\hiprt_tutorial.exe --small
# Dragon with mirror floor and glass sphere, saved as dragon.png:
.\build\bin\hiprt_tutorial.exe --dragon
# Antialiasing: average 16 jittered camera samples per pixel:
.\build\bin\hiprt_tutorial.exe --dragon --samples 16
# Path tracing in the Cornell room, saved as cornell-path.png:
.\build\bin\hiprt_tutorial.exe --cornell --integrator path --samples 64
# Same six-million-instance scene, camera, sample count and depth in both modes:
.\build\bin\hiprt_tutorial.exe --integrator ray
.\build\bin\hiprt_tutorial.exe --integrator path
# Optional replacement for the shared dragon mesh (Z-up):
.\build\bin\hiprt_tutorial.exe --small --ply .\tests\fixtures\orientation_z_up.ply
```

The default mesh is the same separately downloaded dragon used by Detail,
at `ply/Dragon-ply/Dragon-ply.ply` in the source tree. Its path is configured
at build time, so the executable also finds it when run from `build\bin`.
The asset is not bundled; a missing or invalid file is reported as an error,
not silently replaced. `--ply FILE` overrides it with another Z-up triangle
PLY, shared by the two mesh placements in the group scenes or used once in
the dragon-and-mirror scene. Only one of `--small`, `--dragon` and `--cornell` may be selected.
Every invocation renders exactly one frame, without warmup or repeated measurements.
`--samples N` applies to every scene and accepts a positive integer
(default 1). The console reports the selected sample count separately
from the single rendered frame.
`--integrator ray|path` defaults to `ray`. Path images use a `-path` suffix:
`scene-path.png`, `dragon-path.png` or `cornell-path.png`, leaving the corresponding
ray image untouched. The selected integrator and maximum secondary-ray depth
(three in the executable) are printed with the sample count.
The automated tests use small,
bundled PLY fixtures and do not require the external dragon.

After the device information, expect `Diamond mesh built: 24 vertices, 8 triangles, 1 geometry BVH.`,
`Analytic sphere built: 1 AABB, no triangles.`,
`Box mesh built: 36 vertices, 12 triangles, 1 geometry BVH.`,
`Analytic torus built: major radius 0.8, minor radius 0.25, 1 AABB.`,
`PLY mesh built: 1 shared geometry BVH.`,
`Scene BVH built: 6000000 instances (2250000 spheres + 750000 diamond meshes + 1500000 PLY meshes + 750000 box meshes + 750000 tori), 5 shared geometry BVHs.`
and `Rendered 3840 x 2160 pixels to scene.png.`
All six million instances are uploaded and included in the scene BVH; there
is no application-side visibility culling or automatic reduction on allocation
failure. This mode needs substantial CPU/GPU memory. Progress messages and
device-wide free-memory snapshots show the setup stages; the memory delta is
not a peak measurement and can be affected by other GPU users.

Separate one-time wall-clock measurements cover geometry setup/builds, CPU
instance generation, scene preparation/upload/creation, and scene BVH building
(including scratch allocation and GPU completion). The CPU description vector
is released after upload.

The renderer renders the static scene **once** and prints one measurement for:

- **GPU render:** HIP event timestamps around the image kernel on the same
  default stream; includes traversal, shading and shadow rays, excludes readback.
- **Image download:** synchronous GPU-to-CPU copy, measured with `steady_clock`
  after GPU render completion.
- **Render total:** wall time including host submission, event/synchronization
  overhead, GPU rendering and download. It is not simply GPU time plus copy time.

PNG encoding/saving happens afterward and is excluded. There are no warmup
renders or timing statistics; a single measurement may include cold-start
effects and fluctuate between runs. The large scene uses a wider, oblique camera view of the
lattice and a repositioned point light; the small view frames the eight-object group.
Many instances are offscreen or occluded, and camera rays retain their 100-unit
far limit. This tests static-scene traversal and culling, not six million
simultaneously visible objects, BVH animation, or complete display FPS.
The two camera views are not a controlled scaling comparison.

#### Shared tutorial materials

Each `SurfaceMaterial` contains eight floats (32 bytes).
The six-million-instance scene keeps eight `SurfaceMaterial` values (256 bytes)
and 6,000,000 32-bit IDs (24,000,000 bytes), rather than 6,000,000 full
materials (192,000,000 bytes). This saves 167,999,744 bytes, or about 160.217 MiB,
in explicit GPU buffers. `Material storage` prints the actual palette/ID
sizes. If every material is unique, IDs add four bytes per instance instead
of saving memory; the eight-instance test mode therefore uses 32 extra bytes.
Material rotation changes only the per-instance material assignment, not the
shared geometry or palette size. Across 750,000 groups there are 4,500,000
Phong objects, 750,000 mirrors and 750,000 glass objects. Glass PLY replacements
must be consistently wound, closed solids; the loader does not validate
watertightness.

Before adding the box and torus, an alternating A/B comparison on the RX 9070 XT
used the previous 100 x 100 x 100 groups of three spheres and three diamonds
(six materials), rendered at the former 640 x 360 resolution. At that time,
materials contained five floats (20 bytes), before adding `reflectivity`.
With unchanged scene/camera/light settings and byte-identical
PNGs between material-storage variants, it measured:

| Metric | Per-instance materials | Shared palette + IDs |
| --- | --- | --- |
| GPU memory delta after scene setup (device-wide, not peak) | 2109.500 MiB | 2017.938 MiB |
| GPU render mean | 2.037 ms | 2.013 ms |
| Render total including download | 2.424 ms | 2.406 ms |
| Scene preparation/upload/creation | 247.776 ms | 266.576 ms |

Each mode ran six measured processes after discarding an initial process,
with five warmup frames and twenty measured frames per process. GPU means
varied from 1.957 to 2.098 ms before and 1.934 to 2.096 ms afterward:
there was no measurable rendering regression in this workload, but the
roughly 1% average difference should not be interpreted as a proven speedup.
CPU material hashing adds about 19 ms to the one-time setup. The benchmark
used only six unique materials; mostly unique palettes can behave differently.
These historical timings are not directly comparable to the current five-shape scene.

The shared diamond has local vertices at the six unit-axis endpoints and
eight outward-facing triangle faces. Both dragon placements reference a single
PLY vertex buffer and geometry BVH, with different Y rotations and a nonuniform
scale on the rightmost dragon. The five-million-triangle asset is loaded, fitted
and uploaded once, not once per instance. Temporary CPU mesh buffers are released
after upload. The three spheres also share their geometry BVH.
The box has twelve outward-facing triangles (36 unindexed vertices), handled
entirely by HIP RT's built-in triangle intersection, just like the diamond.
It adds no custom box intersection code.
The renderer uses
`hiprtSceneTraversalClosest` instead of `hiprtGeomTraversalClosest`; HIP RT
applies the instance transforms during traversal.
The sphere uses `hiprtPrimitiveTypeAABBList` with one box from (-1, -1, -1)
to (1, 1, 1), not a triangle mesh. HIP RT traverses its BVH, then calls our
GPU `intersectFunc` for a possible hit. Geometry type 0 selects the unit-sphere
intersection; the function table has one ray type and context-assigned geometry slots.
No custom payload is needed because every sphere is the same local unit sphere.
Each distinct torus geometry has its own slot pointing to its GPU radii payload.
Thus multiple torus geometries with different radii can coexist, while instances
of one torus share the same bounds, radii and geometry BVH.
Standalone `Geometry::trace()` selects `hiprtGeomCustomTraversalClosest` for
custom shapes instead of the triangle-only `hiprtGeomTraversalClosest`.
Scene traversal supports meshes and custom shapes together; every scene and
shadow query receives its scene's function table.

For spheres, the callback constructs the polynomial for
`dot(O + t*D, O + t*D) = 1` and uses the shared `quadratic_roots` solver,
chooses the nearest root inside `[ray.minT, ray.maxT]`, and returns `t` and
the outward local normal `O + t*D`. A box hit alone is not a sphere hit.
Both roots are considered, including rays starting inside the sphere.
It keeps `dot(D, D)` rather than assuming a unit direction, since instance
scaling changes local ray lengths. Translation and uniform scale place and
resize the sphere; nonuniform scale produces an ellipsoid. Existing
inverse-transpose normals, materials, Phong lighting and any-hit shadows
work for meshes, spheres and tori.

The torus uses one AABB with half-extents `(R + r, r, R + r)`, then solves
the local implicit equation
`(dot(P, P) + R*R - r*r)^2 - 4*R*R*(P.x*P.x + P.z*P.z) = 0`.
Substituting `P = O + t*D` produces a quartic with up to four real roots.
The callback selects the first root inside the ray interval and returns
the analytic outward normal; the hole remains empty for camera and shadow rays.
Sphere and torus reuse the float-only solvers in `src/math/polynomial.h`.
The torus coefficient construction is shared with the production renderer in
`src/math/torus_roots.h`: recentering and power-of-two scaling keep the quartic
numerically well-scaled. There is no triangulation or sphere tracing.

The example camera is at (0, 0, 10),
looking at the origin with +Y as up. Its 42-degree vertical
field of view frames both rows of four objects.
Change the camera arguments in `main` to move, aim or roll it, or change its
field of view; the kernel needs no edits. FOV must be finite and strictly
between 0 and 180 degrees; position and target must differ, and up must be
nonzero and not parallel to the viewing direction. All input vectors must
be finite. The up direction need not already be perpendicular to the view.
Directions whose squared lengths overflow or underflow to zero in `float`
are rejected explicitly instead of producing invalid camera rays.

The GPU maps pixel sample positions to a view plane at unit distance with half-height
`tan(vertical_fov / 2)`. The aspect ratio keeps the triangle's proportions
correct. `generate_ray()` assumes positive image dimensions and in-range
pixel coordinates and offsets inside the pixel, enforced by the renderer.
Misses use a dark background.
The local lighting term uses Lambert diffuse plus a Phong reflection-vector highlight:

```text
N = normalized world normal
L = normalized direction toward the light
V = -incoming_ray.direction
R = 2 * dot(N, L) * N - L
diffuse = base_color * max(0, dot(N, L))
specular = specular_strength * pow(max(0, dot(R, V)), shininess)
direct = (diffuse + white * specular) * illumination
```

For a directional light, `illumination` is white with unit strength.
For a point light, `L = normalize(light_position - hit_position)` and
`illumination = light_color * intensity / distance_squared`, tinting both
diffuse and specular. An exact hit at the point source is left unlit because
the ideal inverse-square model is singular there.

For reflectivity `r`, outgoing color is
`(1 - r) * direct + r * reflected_color`. The reflection direction is
`D - 2 * dot(D, N) * N`. An iterative ray loop accumulates floating-point
radiance and carries a color throughput multiplied by `r` at each reflection;
clamping happens only when writing the final RGB bytes. Every reflected hit
receives the same material lookup, local shading and shadow testing as a
camera hit. Pure mirrors skip local shading and its shadow ray.
Reflected misses see the dark background. At the depth limit, local lighting
is still evaluated, but the untraced reflection contributes zero; a zero
reflection limit evaluates only the primary surface's local term.

Reflection origins use the same fixed 0.001-unit bias, with its sign selected
from the outgoing direction and geometric normal so back-face reflections
also avoid self-intersection. Reflected rays use float maximum distance,
not the camera's 100-unit far limit. Nonreflective materials stop immediately.
The ray integrator has no GPU recursion, rough reflection or stochastic
surface scattering. The separate path integrator is described below.

#### Tutorial glass

The dragon scene adds a smooth, closed glass sphere with `transmission = 1`
and `ior = 1.5`, resting on the mirror floor without intersecting the dragon.
`src2/dielectric.h` reuses the existing vector refraction helper for Snell's
law. The outward geometric normal distinguishes entering air-to-glass from
leaving glass-to-air. The exact unpolarized dielectric Fresnel equations
determine reflection strength: 4% at normal incidence for IOR 1.5, rising
toward grazing angles; total internal reflection sends all energy into
reflection. An index of 1 bends nothing and produces no dielectric reflection.

For mirror reflectivity `r`, transmission `t` and Fresnel reflectance `F`:

```text
local_weight        = (1 - r) * (1 - t)
reflection_weight   = r + (1 - r) * t * F
transmission_weight = (1 - r) * t * (1 - F)
```

These weights sum to one. Default `t = 0` preserves the old diffuse/mirror
behavior and images. Transmission is clear and untinted; `base_color` affects
only the local diffuse term. Both outgoing rays use the signed surface bias
and see the same scene, materials, lights and background.

The ray integrator follows transmission immediately and defers the reflected sibling
on a small depth-first stack. There is at most one deferred ray per depth;
the validated maximum of eight guarantees the stack fits. Each branch has
its own throughput and depth. At the limit, local shading remains, but
untraced reflection/transmission contributes zero. There is no random branch
selection, throughput cutoff or recursive GPU call. Scenes without transmission
select a separate compile-time kernel specialization that omits the glass stack.

That mode is classical Whitted-style ray tracing, not path tracing. Both modes assume
separate closed solids surrounded by air (or a camera inside one solid);
nested/overlapping dielectric media, absorption, dispersion and rough glass
are not modeled. Shadow rays deliberately retain opaque binary visibility,
including for glass. With area lights, random path continuation can discover
refractive caustics, but there is no specialized caustic sampling; those paths
may be very noisy.

#### Tutorial path tracing: first diffuse step

The path integrator is a separate compile-time GPU kernel specialization, not
a runtime branch at every bounce. The original ray integrator retains its
deterministic dielectric splitting and Phong lighting. Both modes share geometry,
material palettes, traversal, normal transforms, ray bias, image buffers and
timing code; switching does not duplicate the six-million-instance scene.

For now the non-mirror/non-glass part of a path material is **Lambertian**:
`f = base_color / pi`. `specular_strength` and `shininess` still affect the
ray integrator, but not this initial path model. Consequently direct lighting
is also normalized by `1/pi`, unlike the historical ray shading; equal light
values do not imply equal image brightness or identical material appearance.
Diffuse normals face the incoming ray, so finite planes work from either side.

Each hit adds explicitly shadow-tested point/directional lighting, weighted by
`(1 - reflectivity) * (1 - transmission)`, and samples an area light when present.
Point/directional lights are delta sources and are evaluated without MIS.
A miss returns the existing constant background as environment radiance;
the background is not sampled explicitly.

`src2/path_scattering.h` samples exactly one continuation:

- Choose the perfect-mirror component with probability `reflectivity`.
- Otherwise choose glass with probability `transmission`; choose reflection
  with exact Fresnel probability `F`, transmission with `1-F`. TIR always reflects.
- Otherwise sample a cosine-weighted hemisphere, with PDF `cos(theta)/pi`.

The component weights cancel their selection probabilities. Diffuse
`f * cos(theta) / PDF` reduces to `base_color`; it must not be multiplied by
another cosine or mixture/Fresnel factor. Dielectric transmission additionally
scales radiance throughput by `(eta_incident / eta_transmitted)^2`; entry/exit
factors cancel through an air-surrounded solid. Mirror/reflection throughput is
unchanged. No secondary-ray tree or deferred-ray stack is needed.

All samples accumulate unclamped floating-point radiance before one final RGB
conversion. Pixel/sample seeds are reproducible; one sample still uses a
pixel-center primary ray, but its surface continuation is stochastic.
More samples reduce noise, rather than adding repeated timed frames.

The fixed depth limit bounds every path; untraced light at the limit is omitted.
This first step has no Russian roulette, microfacet
gloss, nested dielectric media, transparent shadow rays, caustic-specialized
sampling, or HDR tone mapping/sRGB encoding. Direct-light visibility queries
still treat glass as opaque. Those are later extensions, not silently approximated features.
Use equal resolution, sample count and depth for a cost comparison; equal-quality
comparisons also need to account for path noise and the different diffuse model.

#### Tutorial rectangle lights and MIS

The Cornell room uses eight instances: five walls, a matte sphere, a glass
sphere and a downward-facing ceiling emitter, sharing just the plane and sphere
geometry BVHs. The rectangle sits below the ceiling to avoid coplanar
intersections. Its RGB emission is radiance, not total power: increasing its
area at unchanged emission increases its total output.

`Scene` collects emitting planes into a small GPU light list (instance ID and
radiance), separate from the material palette. `src2/area_light.h` reads their
HIP RT object-to-world matrices, including rotation and nonuniform scaling.
Sampling chooses one light uniformly and then a uniform point on its world
rectangle. The corresponding solid-angle density is
`distance_squared / (light_count * world_area * light_cosine)`.
The shadow segment is recomputed from the biased origin and bounded at the
sampled point; the sampled emitter itself is not a blocker. Other objects,
including glass, still block these straight shadow rays.

The path integrator combines this next-event estimate with BSDF paths that hit
the emitter, using the existing production power heuristic. Both PDFs are in
solid angle and include their selection probabilities. For diffuse
continuation the full mixture PDF is
`(1-r) * (1-t) * cos(theta) / pi`, not just the conditional hemisphere PDF.
Camera hits and hits after a delta reflection/refraction receive full emission.
At the depth limit there is no competing BSDF path, so the light sample gets
full weight. These rules avoid both double-counting and darkening at the limit.

The ray integrator also samples rectangles for direct lighting and soft
shadows, retaining its deterministic mirror/glass branching. Its rectangle
response uses the existing diffuse/Phong heuristic divided by pi, whereas its
historical point/directional response is unchanged. With no diffuse indirect
continuation, it needs no MIS. Sampling a finite light for a shadow ray does
not turn the ray integrator into a path integrator.

Scenes without emitters select kernel specializations that omit all area-light
sampling, PDF state and emission lookup; the original six-million-instance ray
image remains unchanged. More samples per pixel smooth the stochastic soft
shadows in either mode, still within one rendered and timed frame.

#### Tutorial antialiasing

One sample per pixel retains the previous pixel-center ray and image.
For more samples, each camera ray uses an independent pseudorandom offset
inside its pixel. The tutorial reuses `Random` from `src/math/random.h` and
the production camera's pixel/sample seed scheme. Seeds depend only on pixel
coordinates and sample index, so repeated invocations are reproducible.
Samples are accumulated within one frame, not across repeated renders.

Every camera sample includes the complete reflection/refraction/shadow computation.
The renderer sums unclamped floating-point radiance, divides by the sample
count, then converts to RGB bytes. The subpixel offsets perform pixel-area
supersampling, not image-space blur. With rectangle lights each sample also
samples light visibility; path mode additionally samples surface transport.
It needs no per-sample image
buffers and still downloads only one RGB image per frame.

Before simplifying the tutorial to one render per invocation, a single-process
comparison per setting on the RX 9070 XT used the same 4K dragon-and-mirror
scene, five warmup frames and twenty measured frames:

| Samples per pixel | GPU render mean | Render total mean including download |
| --- | --- | --- |
| 1 | 10.954 ms | 12.624 ms |
| 4 | 31.316 ms | 33.183 ms |
| 16 | 118.151 ms | 119.997 ms |

The 1-sample PNG was byte-identical to the previous renderer. More samples
smooth silhouettes, small details, highlights and their reflections, with
remaining sampling noise at finite sample counts. Timings exclude scene
setup and PNG saving and are not a repeated-process performance study.

Specular is evaluated only when both light and viewer are on the normal's
front side. Unlike diffuse lighting, highlights move when the camera moves,
even on a flat triangle. This is classic Phong reflection-vector lighting,
not Blinn-Phong (which uses a halfway vector), and not smooth vertex-normal
interpolation ("Phong shading"). Surface data is uploaded once per distinct
`SurfaceMaterial`, with one material ID per instance.

HIP RT returns an unnormalized object-space geometric normal, oriented by
triangle vertex winding. The renderer obtains the inverse transform using
`hiprtGetWorldToObjectFrameMatrix`, multiplies the normal by its transposed
linear part, and normalizes with `hiprt::normalize`. This inverse-transpose
rule keeps normals perpendicular to the surface under nonuniform scaling;
the ordinary vector transform would be incorrect. Translation does not
affect normals. In ray mode the normal is not flipped toward the camera: light behind
the oriented surface contributes no local lighting, with no ambient term.
Mirror reflections are independent of this direct-light test.

For a front-lit point/directional-light hit, a second ray tests visibility using
`hiprtSceneTraversalAnyHit` and `hiprtTraversalHintShadowRays`. Unlike closest-hit
traversal, this query only needs to find any blocker. A hit makes the diffuse
and specular contributions zero, producing a hard shadow in the local lighting
term (there is no ambient light); mirror reflections remain visible.
The shadow ray starts at `point + 0.001f * normal` to avoid self-intersection.
This fixed world-space bias is appropriate for the tutorial's scale, not a
general solution for extremely small or large scenes. The ray's default
maximum distance is float maximum: directional lights are infinitely distant,
so the camera's 100-unit far limit does not apply to shadow rays.
For a point light, the shadow direction and distance are recomputed from
the biased origin. The ray stops just before the light, so geometry beyond
the light cannot cast a shadow on the hit.

This is classical ray tracing with local Phong lighting and ideal mirror
reflections, not an energy-conserving physical light-transport model. Local
diffuse and specular are added, mixed with reflections, and clamped to [0, 1]
when storing RGB bytes. There is no diffuse indirect lighting, ambient term,
or gamma correction yet.

`scene.png` is written in the current working directory, overwriting an
existing file. To keep it beside the executable, run from `build\bin`:

```powershell
Push-Location .\build\bin
.\hiprt_tutorial.exe
Pop-Location
```

Open the image in any PNG viewer. `save(path)` always writes PNG data.

The final message is `HIP RT context destroyed.` A HIP-compatible AMD GPU and
the same SDK/runtime as the renderer are required. The tutorial now links the
HIP RT device library as well as the host API.

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
| `--unified-traversal[=true\|false]` | Experimental shared scene traversal for base custom geometry, meshes and instances. Also supported in render configs. | `false` |

Quote CLI paths containing spaces. Unknown/abbreviated options, duplicate
options within one input, invalid values and positional arguments are rejected.
Dimensions and sample counts must be positive integers; both adaptive thresholds
must be finite and positive. Dimensions must fit the renderer and BMP limits.
An explicitly selected GPU must exist and match the compiled GPU architecture.

PNG is lossless and stores the same pixels as BMP, usually in a much smaller
file. Unsupported or missing image extensions are rejected before GPU setup.
Existing output images are overwritten.

### Experimental unified traversal

The default production path queries base custom geometry, the base triangle
mesh and explicit instances separately. `--unified-traversal` instead builds
one HIP RT scene containing identity instances of the base custom/mesh BVHs
and the existing transformed placements. The lower-level geometry BVHs and
per-prototype buffers are still shared. The analytic room remains a separate
intersection in both modes.

Root instance IDs select the base custom or mesh shading path; placement IDs
are offset back into the existing instance/material/light buffers. Surface
shading is shared between modes, preserving logical object IDs, CSG leaf
materials, prototype-space textures and emitter PDFs. Closest-hit and bounded
any-hit queries use the shared scene. Separate compile-time `Unified` and
`Instanced` kernel variants keep unused traversal code out of the selected path.

To reproduce an A/B comparison, use identical settings and change only the flag:

```powershell
.\build\bin\hiprt_demo.exe --scene detail --width 1280 --height 720 --sampling fixed --samples 128 --batch-size 8 --unified-traversal=false -o split.png
.\build\bin\hiprt_demo.exe --scene detail --width 1280 --height 720 --sampling fixed --samples 128 --batch-size 8 --unified-traversal -o unified.png
```

Measured on the RX 9070 XT / gfx1201 with HIP RT 3.0.2: mean GPU render time
over three runs per mode, after discarding an initial run per mode. Modes
were alternated, using the same executable, deterministic fixed sampling and
batch size 8. GPU times exclude loading/building the scene and saving images.

| Scene | Resolution / samples | Split | Unified | Unified change |
| --- | --- | --- | --- | --- |
| detail | 640 x 360 / 64 | 0.451 s | 0.525 s | +16.5% |
| csg_text | 640 x 360 / 64 | 1.385 s | 1.427 s | +3.0% |
| instancing | 640 x 360 / 64 | 0.565 s | 0.567 s | +0.4%, effectively unchanged |
| detail | 1280 x 720 / 128 | 3.207 s | 3.773 s | +17.6% |

Detail and CSG-text images were byte-identical across modes, including the
larger detail comparison. In the instancing image, 15 of 230400 pixels differed
by at most one RGB code value. GPU regression tests compare hits, materials,
normals, CSG leaf IDs, bounded visibility and emitter PDFs; both fixed and
adaptive integration tests also exercise unified rendering.

This experiment does **not** show a performance benefit, so split traversal
remains the default. Unlike the tutorial's many independently bounded
placements, ordinary scenes add only a few broad base-geometry bounds to the
top-level scene. A shared top-level traversal is not automatically cheaper.
Determining the dominant instruction/memory bottleneck still requires profiling;
these measurements alone do not attribute the render time to CSG, shading or
register pressure.

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
| [Colored CSG](scenes/csg_colored.hipscene) | `--scene .\scenes\csg_colored.hipscene` | None. |
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
forms default to Y; `center` is the midpoint of their height interval.
Cylinders optionally accept `axis x`, `axis y` or `axis z` after `height`.
The interval runs from `center[axis] - height/2` to `center[axis] + height/2`;
cones, paraboloids and hyperboloids remain Y-aligned:

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

### Analytic boxes

```text
box { center [0, 1.5, 0] size [3, 3, 3] material red }
```

`size` contains the full positive X/Y/Z dimensions. Boxes use exact slab
intersections, not triangles, and support CSG and instancing. They are
axis-aligned in their own coordinate system; rotate a placement using an instance.
Emissive boxes currently have no direct-light sampler.

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
prototype material, including built-in cat detail materials and CSG leaf bindings.

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
17 prototypes: varied geometry, every material/texture kind, rotated tori, nonuniform
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

A `csg` object combines boxes, closed quadrics (sphere, ellipsoid, cylinder, cone,
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

Set a default material on the `csg` object. A leaf may optionally specify its own
`material name`; otherwise it inherits that default. Each visible boundary uses
the material of the leaf that supplies its normal. In a difference, the cutter
therefore supplies the material of the new cut wall, whose normal points out of
the remaining solid. Material bindings belong to the object, not the geometry
tree, and are preserved through cached intersections and instance placement.
Meshes, rectangles and room surfaces cannot be CSG operands. A CSG object can
itself use an emissive material. The [CSG quadrics demo](scenes/csg_quadrics.hipscene)
shows nested unions and cutouts with all five new shapes. Quadric caps are part
of the solid and participate in Boolean cuts just like their curved sides.

The [colored CSG scene](scenes/csg_colored.hipscene) shows one object: the
intersection of a red box and a blue sphere, minus three green X/Y/Z cylinders.
A smaller sphere leaves narrow red face rings and larger blue curved surfaces;
the wider cylinders expose more green bore walls. It needs no external assets
and uses no triangles for this solid.

```powershell
.\hiprt_demo.exe --scene ..\..\scenes\csg_colored.hipscene --output .\csg_colored.png --width 1920 --height 1080 --sampling fixed --samples 1024
```

Text operands use the same font, value, size, depth and position fields as
standalone text; omit the leaf material to inherit the CSG default:

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
