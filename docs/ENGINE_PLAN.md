# Shark Engine Plan

- **Status:** Active working plan
- **Plan date:** July 11, 2026
- **Last updated:** July 18, 2026
- **Latest completed:** `S-002A` - procedural daylight baseline after `T-001`
- **Next increment:** `T-002` - canonical terrain spatial queries

## 1. Project direction

Shark will be a Windows-first 3D graphics and physics simulation engine built
directly on modern Direct3D 12. It is not initially a general-purpose game
engine. The near-term product is an **Environment Lab**: a small executable in
which we can fly a camera through a sky, inspect textured terrain, turn rain on
and off, collide objects with the terrain, and evolve visually convincing water
into a physically meaningful surface-water simulation.

The first vertical slice, **Environment Lab 0.1**, will contain:

- a controllable free-fly camera;
- a basic procedural daylight sky and sun, followed later by HDR
  image-based lighting;
- heightmapped terrain with physically based textures;
- bounded, wind-driven GPU rain with splashes and visual wetness;
- a visually convincing water surface;
- diagnostics output with frame and GPU timings.

The next slice, **Simulation Lab 0.2**, will add:

- a deterministic fixed simulation clock;
- terrain height, normal, and ray queries shared with rendering;
- basic rigid bodies colliding with terrain;
- a conservative shallow-water solver; and
- rendering driven by the simulated water state.

The later **Coupled World 0.3** slice will add measured rainfall feeding water,
runoff between terrain tiles, buoyancy, and eventually two-way water/body
coupling. Controllable characters, animation, an editor, scripting, and broader
game systems come after this environment foundation is stable.

## 2. Working agreement

Every implementation step is a small, independently understandable increment.
For each increment:

1. We agree on one outcome and its explicit non-goals.
2. Codex implements only that scope and runs the relevant checks.
3. Codex reports the changed files, visible result, checks run, limitations, and
   one suggested commit message.
4. The repository owner reviews `git diff`, stages the desired files, commits,
   and pushes manually.
5. Work on the next increment starts only after the owner confirms the handoff.

Codex will not stage, commit, push, rebase, merge, or alter remotes unless the
owner explicitly asks for that operation. Generated build output, downloaded
dependencies, asset caches, captures, and logs will never be proposed for a
commit.

An increment is complete only when:

- it delivers one observable behavior or one cohesive infrastructure change;
- Debug and Release still build once those configurations exist;
- relevant automated tests pass;
- graphics changes produce no D3D12 debug-layer errors;
- changed decisions are reflected in documentation;
- the diff contains no unrelated refactoring or formatting; and
- the owner has a clear way to reproduce the result.

There is no arbitrary line-count limit. Direct3D 12 setup can be verbose; the
test is whether the change can be reviewed, verified, and reverted on its own.

## 3. Simulation honesty levels

The roadmap labels effects so visual realism is not confused with physical
simulation:

| Label | Meaning | Example |
|---|---|---|
| **V - Visual** | Convincing appearance without a conserved physical state | Rain streak particles or normal-mapped water |
| **S - Simulated** | A real-time mathematical model advances meaningful state | Rigid-body contact or shallow-water depth and momentum |
| **C - Coupled** | Two simulated systems exchange forces or conserved quantities | Rain adds measured water volume or water applies buoyancy |

We will first make an effect visible, then simulate it where that creates useful
behavior, and finally couple systems after each one is independently stable.

## 4. Technical baseline and durable decisions

These choices define the starting line. A later change requires a small
Architecture Decision Record (ADR) explaining why.

| Area | Decision | Reason |
|---|---|---|
| Platform | Windows 11, x64 desktop | Direct3D 12 is the purpose of the project; a cross-platform layer would slow the first vertical slice |
| Language | C++20 with MSVC `14.50` LTS (`v145`) strict conformance | Modern facilities on a three-year LTS compiler family |
| Windowing | Native Win32 | Minimal dependency surface and direct DXGI integration |
| Build | CMake 4.2+, `Visual Studio 18 2026` generator, and vcpkg registry commit `f87344cac03158cbf1467264565f1fd36b382a24` | Reproducible command-line and Visual Studio builds |
| Graphics API | Direct3D 12 through a narrow typed RHI | Keeps D3D objects below the renderer without inventing an unneeded Vulkan abstraction |
| Runtime | Retail DirectX 12 Agility SDK `1.619.4`, pinned in `F-002` | Current stable D3D12 runtime; preview SDKs stay off `main` |
| Shaders | HLSL compiled to DXIL by retail DXC `1.9.2602.24`, pinned in `F-002` | Reproducible Shader Model 6 builds and PIX source debugging |
| Required GPU baseline | Feature Level 12_0+ and Shader Model 6.0+ | Runs the first environment on a broad D3D12 hardware base with conventional descriptor tables |
| Modern GPU profile | Shader Model 6.6+ and Resource Binding Tier 3, capability-gated | Enables direct descriptor-heap indexing when material scale justifies a bindless path |
| Ultimate features | Feature-query and enable individually | Feature Level 12_2, DXR, mesh shaders, VRS, and sampler feedback are enhancements, not startup dependencies |
| Barriers | Enhanced barriers when supported, legacy encoder fallback | Enhanced barriers are optional at the driver level |
| Rendering | Forward raster first, evolving to clustered Forward+ when needed | Sky, terrain, rain, and water do not justify a deferred renderer initially |
| Simulation | Fixed 60 Hz tick with render interpolation | Stable physics behavior independent of rendering frame rate |
| Physics | Shark-owned interface and initially limited custom solver | Lets us learn and control the simulation; a mature library may later serve as an optional backend or comparison oracle |
| Fluids | 2.5D shallow-water depth/momentum model first | Suitable for puddles, runoff, rivers, and flooding without the scope of full 3D fluid dynamics |
| Threading | Main-thread engine and direct GPU queue first | Correctness and instrumentation precede job systems and async compute |
| Content | Procedural or clearly licensed small test assets | Keeps the repository reproducible and legally clean |

Version pins are updated only in dedicated dependency increments. Preview
Agility SDKs, preview Shader Models, and experimental APIs belong on disposable
experiment branches, not on `main`.

### Initial dependency budget

Keep third-party code deliberately small and pin every dependency:

- retail DirectX 12 Agility SDK and DXC for the runtime and shader toolchain;
- retail `Microsoft.Direct3D.WARP` for a reproducible software-GPU smoke-test
  path;
- DirectX-Headers/`d3dx12.h` and DirectXMath for official API helpers and math;
- DirectXTex/`texconv` for offline texture preparation plus a small DDS runtime
  loader;
- WinPixEventRuntime for GPU event markers;
- `spdlog` for structured development logging; and
- Catch2 for CPU unit and integration tests.

D3D12 Memory Allocator may be added when placed-resource pools begin. Dear ImGui
waits until live simulation tuning needs a debug UI. A third-party physics engine
is not part of the initial solver; it may later be added behind the physics
boundary as a comparison oracle or optional production backend.

### Known development-machine constraints

The development machine is Windows 11 with both an NVIDIA discrete GPU and an
Intel integrated GPU. G-001 now enumerates adapters by high-performance
preference, logs every candidate, supports exact session-index selection, and
provides an explicit packaged-WARP smoke path. WARP is for correctness tests,
not performance validation.

The July 12 prerequisite check reports the `F-002` gate ready with no blocking
failures. Visual Studio 2026, MSVC 14.50 LTS, CMake 4.3.1, vcpkg, Windows SDK,
Graphics Tools, and Ninja are available. PIX remains a non-blocking requirement
only for manually inspecting G-007 captures; the marker runtime is restored by
the project. Global DXC is intentionally unnecessary because Shark restores a
pinned project-local copy. See
[Windows development setup](WINDOWS_SETUP.md).

## 5. World, math, and color conventions

These conventions are fixed before the first camera or shader:

- right-handed world coordinates;
- `+Y` is up, `+X` is right/east, and forward is `-Z`;
- meters, kilograms, seconds, radians, meters/second, and meters/second squared;
- rainfall represented physically as meters of water per second (equivalent to
  volume per area per second);
- Direct3D normalized depth range `[0, 1]` with reversed-Z;
- row-major CPU/HLSL matrices, row vectors, and `mul(vector, matrix)`; DXC uses
  the matching row-major flag;
- linear HDR scene lighting, with sRGB decoding for color textures and tone
  mapping only at final output;
- seeded random streams for repeatable simulation scenarios; and
- `float` local coordinates initially, with origin rebasing deferred until world
  scale demonstrates the need.

GPU particle and fluid results are tolerance-tested; bitwise cross-vendor
determinism is not promised.

## 6. Architecture

Shark begins as a **layered monolith**: clear module boundaries in one engine
library, not a collection of DLLs or tiny libraries. Targets are split only when
independent build or reuse value appears.

The diagram shows orchestration and one-way data flow. The simulation
coordinator calls stateful systems and publishes a snapshot; `World` and
`Physics` never depend on each other in a compile-time cycle.

```mermaid
flowchart TD
    App[Shark Sandbox / composition root]
    Core[Core: logging, time, math, IDs, results]
    Platform[Platform/Win32: window, input, files]
    Simulation[Simulation coordinator / fixed clock]
    World[World: transforms, camera, lights]
    Snapshot[Immutable CPU/GPU simulation views]
    Assets[Assets: source data, textures, shaders]
    Terrain[Terrain: canonical height, material, and soil data]
    Weather[Weather: wind and precipitation state]
    Physics[Physics: bodies, contacts, solver]
    Fluids[Fluids: water depth and momentum]
    Renderer[Renderer: passes and scene extraction]
    Graph[Render/Compute Graph]
    RHI[RHI/D3D12: device, queues, resources, descriptors]
    Diagnostics[Diagnostics: tests, captures, timings]

    App --> Core
    App --> Platform
    App --> Simulation
    Simulation --> World
    Simulation --> Weather
    Simulation --> Physics
    Simulation --> Fluids
    Terrain --> Physics
    Terrain --> Fluids
    World --> Snapshot
    Weather --> Snapshot
    Physics --> Snapshot
    Fluids --> Snapshot
    Snapshot --> Renderer
    Terrain --> Renderer
    Fluids --> Graph
    Renderer --> Graph
    Graph --> RHI
    Assets --> Renderer
    Simulation -. telemetry .-> Diagnostics
    Graph -. telemetry .-> Diagnostics
```

### Module responsibilities

| Module | Owns | Must not own |
|---|---|---|
| `Core` | logging, assertions, results, time, IDs, math conventions, seeded RNG | window, D3D12, scene policy |
| `Platform/Windows` | Win32 window, messages, input, timing, file watching | renderer or simulation state |
| `RHI/D3D12` | DXGI, device, queues, fences, swap chain, heaps, descriptors, command lists, PSOs | terrain, weather, physics concepts |
| `RenderGraph` | pass/resource declarations, order, barriers, lifetimes, queue synchronization | scene mutation or gameplay decisions |
| `Renderer` | frame pipeline, camera data, sky/terrain/water/rain passes, debug views | authoritative physics or fluid state |
| `Assets` | CPU asset records, loading, derived-data keys, shader artifacts | frame scheduling |
| `World` | transforms, cameras, lights, scenario state, immutable frame snapshots | raw GPU resources |
| `Simulation` | fixed clock, subsystem order, input/output exchange, snapshot publication | rendering passes or subsystem internals |
| `Terrain` | authoritative height/material/soil tiles and spatial queries | D3D12 resource ownership |
| `Weather` | wind, precipitation rate, later temperature/humidity/evaporation drivers | terrain infiltration capacity or rain particles as physical rainfall |
| `Physics` | bodies, colliders, broad/narrow phase, contact solver, debug primitives | D3D headers or render meshes |
| `Fluids` | water depth/momentum, solver, conservation accounting, coupling | presentation or material decisions |
| `Diagnostics` | tests, scenario capture, debug HUD, timings, validation output | production simulation policy |

Through S-002A, the bootstrap `RHI/D3D12::Presentation` class still combines
renderer pass orchestration with low-level D3D12 ownership inherited from the
first-pixel increments. The canonical terrain module remains platform
independent and lends only derived presentation data, so no simulation boundary
depends on D3D12. This bootstrap placement is not the durable architecture:
`REN-001` must move pass-specific configuration, statistics, and orchestration
behind the `Renderer` boundary before terrain materials expand it.

### Non-negotiable ownership rules

1. Full-resolution `TerrainData` is the single source of truth for collision and
   the fluid bed. LOD0 rendering uses the same cell split/interpolation exactly;
   coarser visual LODs are derived approximations with a measured error bound and
   never change physics with camera distance.
2. Physics never reads a render mesh or Direct3D resource.
3. Visual rain particles and physical rainfall share `WeatherState`, but particle
   count never determines water volume.
4. The fluid solver owns water state. Rendering receives a read-only snapshot or
   SRV and never mutates the simulation.
5. Render and compute passes declare GPU access to the graph. Individual systems
   do not issue ad hoc barriers or cross-queue waits.
6. Simulation publishes immutable previous/current snapshots. Rendering
   interpolates between them and never advances simulation.
7. No synchronous full GPU readback is allowed in the normal frame loop.

### Frame and simulation flow

```text
Poll input and platform events
  -> accumulate real elapsed time
  -> run zero or more fixed 60 Hz CPU ticks
       1. update weather
       2. consume the newest completed compact fluid-query result, never wait
       3. advance physics and world state
       4. queue versioned weather/body inputs and requested fluid steps
       5. publish the immutable CPU simulation snapshot
  -> interpolate render snapshot
  -> build render/compute graph
       1. advance queued GPU fluid steps with CFL-limited substeps
       2. publish ping-pong fluid render views and enqueue compact query readback
       3. shadows/depth -> terrain/opaque -> sky -> water -> rain -> post/debug
  -> record, submit, present
```

Before the GPU solver exists, the CPU reference can advance inline during a
fixed tick. The normal GPU path is deliberately asynchronous: catch-up ticks
reuse the latest completed compact query and never stall for water. A synchronized
mode exists only for focused verification. This staggered order avoids an
unstable circular solve; iterative two-way coupling is a later milestone.

## 7. Direct3D 12 renderer design

### Device and frame foundation

- Enumerate DXGI adapters using high-performance preference and expose
  `--adapter`, `--warp`, and a capability-report mode.
- Build a `RendererCaps` record using `CheckFeatureSupport`; never infer optional
  support from a feature-level number.
- Before device creation, enable the D3D12 debug layer, optional GPU-based
  validation, and DRED. Configure bounded `ID3D12InfoQueue` and
  `IDXGIInfoQueue` storage plus debugger-break policy without suppressing
  messages. GPU-based validation is an opt-in focused-test mode because it is
  expensive.
- Submission, fence, resize, and present failures query the device-removal
  reason and emit bounded DRED breadcrumb and page-fault details.
- Use a flip-model, triple-buffered swap chain and a reversed-Z depth target.
- Define direct, compute, and copy queue interfaces with monotonic fence
  timelines, but initially submit graphics, compute, and uploads on the direct
  queue. Activate copy or async-compute queues only after the graph owns their
  cross-queue fence handoffs and PIX demonstrates a benefit.
- Use a `FrameContext` per back buffer for command allocators, transient uploads,
  descriptors, timestamps, and a completion set containing every queue fence
  that guards reuse. G-003 establishes one allocator, bounded upload/CPU
  descriptor staging, and one direct-fence checkpoint per context while sharing
  one graphics command list. T-001 uses one fixed eight-query timestamp/readback
  slice per context, backed by one 24-query heap and one 192-byte buffer, and
  reuses each slice only after the same direct-fence checkpoint completes.
  Multi-queue completion sets remain later.
- Defer destruction of GPU resources, descriptors, pipelines, and upload storage
  until every relevant queue fence in their retirement set completes. G-003
  verifies whole-context transient reuse. G-005 creates one persistent root
  signature/PSO, cube vertex/index buffers, checker texture/SRV, and resize-owned
  depth target. S-002 adds a second immutable far-depth sky PSO; S-002A changes
  its visible content from the diagnostic cubemap to procedural daylight.
  T-001 adds terrain vertex/index buffers plus
  solid, wireframe, and bounds PSOs in the same bounded static-upload and
  startup lifetime; shutdown releases every persistent object after the final
  drain. Generic fence-keyed deferred destruction remains later.

### Resources and descriptors

- Expose typed generational handles such as `BufferHandle`, `TextureHandle`,
  `SamplerHandle`, and `PipelineHandle`; raw COM pointers stay in `RHI/D3D12`.
- Start with committed resources and simple correctness. Add placed-resource
  pools and aliasing only after telemetry shows value.
- Keep persistent resources separate from graph-transient resources.
- Use one shader-visible CBV/SRV/UAV heap and one sampler heap with stable
  persistent indices; recycle slots only after their fences complete. Begin
  with conventional descriptor tables while keeping handles bindless-ready.
  G-005 proves the checker at shader-visible SRV slot 0 and one static sampler.
  S-001 retains the uploaded orientation cubemap at slot 1 as an asset-path
  proof. S-002A makes `Skybox` b0-only and removes that dormant texture from
  the per-frame graph; the stable-index allocator remains future work.
- Keep CPU-only RTV and DSV allocators separate.
- Add upload and readback arenas, with a per-frame linear upload ring.
- Expose descriptor use and DXGI video-memory budget in diagnostics before
  texture streaming is attempted.

### Shaders and pipelines

- Compile HLSL at build time with pinned DXC and treat warnings as errors. G-004
  resolves only the vcpkg-restored retail DXC `1.9.2602.24`, compiles explicit
  `vs_6_0`/`ps_6_0` entry points with HLSL 2021, strict and row-major flags,
  emits per-configuration DXIL/C++ headers/PDBs, and tracks shared includes
  through depfiles.
- Retain shader PDB/debug data in developer builds for PIX. G-004 embeds source
  debug information while keeping Debug unoptimized and Release optimized.
- Begin with a small shared root-signature convention using descriptor tables and
  frame/pass constants. Direct heap indexing becomes a separate capability-gated
  increment only after Shader Model 6.6, Binding Tier 3, and a real scale need are
  confirmed. S-002A's bootstrap frame CBV contains the row-major scene and
  translation-free sky matrices plus six packed daylight rows and is visible
  to vertex and pixel stages. The checker cube separately retains one
  single-SRV descriptor table and static sampler. This is not yet a versioned
  renderer-wide layout.
- Key shader artifacts by source/include hashes, entry point, target, defines,
  flags, compiler version, and root-signature version.
- Cache immutable PSOs by structural hash; never compile a surprise PSO in the
  middle of a render pass. G-005 creates one immutable cube/depth PSO
  synchronously during presentation startup; generalized artifact keys and PSO
  caching remain later. S-002 creates a second immutable skybox PSO from pinned
  build-time DXIL and never compiles pipeline state inside the frame loop.
- Add development hot reload only after the offline build pipeline is reliable.

### Render graph growth

The graph is renderer infrastructure, not a scene API. G-006 establishes
whole-resource reads and writes, intended resource states, explicit and
hazard-derived dependencies, and an execution callback that can resolve only
its declared resources. Subresource ranges, attachment policy, and queue
preference remain later extensions.

It grows in three deliberate stages:

1. **Simple:** direct queue, imported committed resources, declaration
   validation, stable hazard-aware topological compilation, cycle rejection,
   and centralized legacy barrier encoding. T-001 keeps this first form and
   executes ordered `Terrain`, `TexturedCube`, and `Skybox` passes with
   explicit texture and input-assembler reads.
2. **Managed:** lifetime analysis, graph-owned transient placed resources,
   resource pooling, subresource scopes, and aliasing.
3. **Optimized:** pass merging, parallel recording, and compute/copy scheduling
   only where PIX proves a benefit.

G-006 provides only a whole-resource legacy-barrier executor. A future
capability-gated increment may add an enhanced-barrier encoder; one model will
then be selected globally for the device lifetime rather than mixed casually
across resources or frames.

### Diagnostics from the first executable

- check every `HRESULT` and name every significant D3D12 object;
- add PIX markers around every pass and major upload; T-001 names the current
  `StaticSceneUpload`, `Frame`, `Terrain`, `TexturedCube`, and `Skybox`
  boundaries;
- expose per-pass CPU/GPU timestamps, draws, dispatches, descriptors, memory,
  and queue waits; T-001 reports direct-queue GPU intervals for the frame plus
  separate `Terrain`, `TexturedCube`, and `Skybox` intervals, while CPU timings
  and broader counters remain later;
- keep a WARP smoke path for device creation and a deterministic basic frame;
- treat zero debug-layer errors as an acceptance gate; and
- test real graphics on both the discrete and integrated adapters when their
  capabilities meet the declared baseline.

## 8. Environment and simulation design

### Sky

1. Load a small DDS cubemap and render it without camera translation (**V**).
2. Add HDR cubemap conversion, diffuse irradiance, and specular prefiltering for
   image-based lighting (**V**).
3. Add sun/atmosphere and volumetric clouds only after terrain, rain, and water
   are stable (**V/S**, later research).

### Terrain and textures

1. Render one procedural or loaded height tile with solid/wireframe debug views.
2. Add exact CPU height, normal, bounds, and ray queries from the same data.
3. Add PBR texture arrays and slope/height or painted blend weights.
4. Add chunks and frustum culling at full resolution.
5. Add derived visual LODs with crack-free seams and measured error bounds.
6. Add streaming, virtual texturing, or mesh-shader meshlets only when scene size
   makes them necessary.

### Weather and rain

`WeatherState` owns wind and precipitation rate. The visual system turns those
values into a bounded camera-relative GPU particle field, streaks, impacts,
splashes, and a non-conserving wetness mask (**V**). Later, hydrology converts
the same precipitation rate into a measured source term for water cells (**C**).

### Rigid-body physics

Start with semi-implicit Euler, terrain contact, spheres, capsules, and boxes.
Use brute-force pair generation before a broad-phase structure because it is
easy to verify. Add analytic primitive contacts, sequential impulses, friction,
and restitution before considering arbitrary convex shapes, GJK/EPA, CCD,
islands, or sleeping.

The first physics proof is simple: a sphere falls, contacts the displayed
terrain, and rests without hovering, penetrating, or changing behavior when the
render frame rate changes.

### Water and fluids

The first water is explicitly visual: Fresnel response, depth tint/absorption,
refraction/reflection approximation, and animated normals (**V**). It is not
called a fluid simulation.

The first simulated fluid is a conservative 2.5D shallow-water grid storing
water depth and horizontal momentum (**S**). A small CPU reference solver comes
first with lake-at-rest, dam-break, dry/wet-front, wall, positivity, and mass
tests. A compute-shader version then matches the reference within documented
tolerances. Stability uses a CFL-limited timestep rather than hiding failures
with arbitrary velocity clamps.

The platform-independent fluid core owns the equations, configuration, CPU
reference, and validation. When the GPU solver arrives, a D3D12 fluid adapter
owns the GPU textures and registers compute work through the render/compute
graph. The renderer receives only declared read-only views of the result. Full
readback exists for focused tests, never as a normal per-frame dependency.

This model supports puddles, terrain runoff, rivers, and flooding. It does not
support overturning waves, jets, breaking ocean waves, or fine volumetric
splashes. FLIP, SPH, or a full 3D Navier-Stokes solver is a separate future
research program.

Coupling proceeds in this order:

1. rainfall adds measured volume to sealed-basin water (**C**);
2. soil infiltration parameters and weather-driven evaporation become explicit
   boundary sinks with a mass ledger (**S**; no closed soil/atmosphere reservoir
   is claimed);
3. water moves across active terrain tiles (**S**);
4. a compact, versioned GPU query result reaches CPU physics asynchronously;
5. water applies approximate buoyancy and drag to bodies (**C**, one-way);
6. bodies become conservative moving-bed/free-volume boundaries for water
   (**C**, the other direction); and
7. the two directions run as stable, staggered coupling (**C**).

## 9. Planned repository layout

The first build increment may create only a subset of this tree. Empty
architecture folders are not committed merely to make the tree look complete.

```text
/
|-- apps/
|   `-- sandbox/               # Executable and subsystem composition
|-- engine/
|   |-- core/
|   |-- platform/windows/
|   |-- rhi/d3d12/
|   |-- render_graph/
|   |-- renderer/
|   |-- assets/
|   |-- world/
|   |-- simulation/
|   |-- terrain/
|   |-- weather/
|   |-- physics/
|   |-- fluids/
|   |   `-- d3d12/
|   `-- diagnostics/
|-- shaders/
|   |-- shared/
|   |-- cube/                 # First camera/depth/resource-bound HLSL proof
|   |-- sky/                  # Static cubemap background HLSL proof
|   |-- terrain/
|   |-- rain/
|   `-- water/
|-- content/
|   |-- source/                # Small licensed source assets
|   `-- scenarios/             # Deterministic test scenes
|-- tools/                     # Asset tools when justified
|-- tests/
|   |-- fixtures/shaders/     # Isolated compiler-failure/rebuild probes
|   |-- unit/
|   |-- integration/
|   `-- gpu/
|-- cmake/                    # Toolchain, shader, and verification helpers
|-- docs/
|   |-- adr/
|   `-- milestones/
|-- scripts/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg.json
`-- vcpkg-configuration.json
```

Initially this produces three targets: `SharkEngine`, `SharkSandbox`, and
`SharkTests`. Module-specific static libraries are introduced only when the
dependency graph or build times justify them.

## 10. Increment roadmap

Each row is intended to become one owner-reviewed manual commit. A milestone is
a useful product checkpoint made from several such commits.

### M0 - Direction

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `P-000` | - | Add this architecture, scope, ownership model, roadmap, and manual Git agreement | `docs: define Shark engine architecture and roadmap` |

### M1 - Reproducible foundation

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `F-001` | - | Document the required MSVC, Windows SDK, CMake, and graphics tools; a check script reports actionable pass/fail results | `build: define Windows toolchain prerequisites` |
| `F-002` | - | Add CMake presets, pinned dependency manifests, `SharkEngine`, `SharkSandbox`, and one passing test; a fresh configure/build/test is documented | `build: add reproducible CMake project skeleton` |
| `F-003` | - | Add results, logging, assertions, and strict warnings; one intentional failure is covered by a unit test | `feat(core): add diagnostics foundation` |
| `F-004` | - | Open, resize, minimize, and close a Win32 window with a stable application loop and input/event boundary | `feat(platform): add Win32 application shell` |

### M2 - First pixels and skybox

This is the shortest responsible path to the first requested visual feature.

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `G-001` | - | Initialize Agility/D3D12, choose the high-performance adapter, log capabilities, and start hardware/WARP with zero debug errors | `feat(gpu): initialize Direct3D 12 device` |
| `G-002` | V | Create a resize-safe flip swap chain and present a clear color for 1,000 frames without validation or live-object errors | `feat(gpu): present a clear-color frame` |
| `G-003` | - | Add three back-buffer-indexed frame contexts, a monotonic direct-queue fence, bounded per-context upload and CPU descriptor staging, and verify transient slots reset only after submission completes | `feat(gpu): add frame resource lifecycle` |
| `G-004` | V | Resolve retail DXC `1.9.2602.24` only from the manifest host tools; compile tracked `vs_6_0`/`ps_6_0` HLSL with warnings as errors; reject malformed/warning fixtures; create one immutable PSO; and record one three-vertex triangle draw per submitted frame | `feat(render): add the first HLSL pipeline` |
| `G-005` | V | Add the `+Y`-up/`-Z`-forward row-vector camera and right-drag/`WASD`/`QE`/`Shift` controls; render one 24-vertex/36-index cube with an `8x8` procedural checker through a root CBV plus one SRV/static sampler; and recreate a `D32_FLOAT`, clear-`0`, `GREATER_EQUAL` reversed-Z target safely across resize | `feat(render): add camera and depth conventions` |
| `G-006` | - | Add a frame-local, single-use direct-queue graph with move-safe owner-scoped imports/passes, explicit plus RAW/WAR/WAW dependencies, stable topological compilation, cycle and callback-access validation, and centralized whole-resource legacy barriers; run the existing cube as one `TexturedCube` pass with exactly two attachment transitions per submitted frame | `feat(render): add minimal render graph` |
| `G-007` | - | Link the pinned WinPixEventRuntime for Debug and Release markers; name `StaticCubeUpload`, `Frame`, and `TexturedCube`; partition one 12-query/96-byte timestamp/readback allocation into three four-query frame-context slices; consume only fence-complete results; and report validated frame/pass samples plus bounded query accounting without adding a normal-frame drain | `feat(diagnostics): add GPU frame instrumentation` |
| `S-001` | V | Load the project-owned `8x8` DX10 DDS orientation cube through a DirectXTex-isolated CPU boundary; reject ambiguous/malformed cube metadata, upload its six sRGB faces in the existing static submission, and create persistent cube SRV slot 1 without changing the visible frame | `feat(assets): load DDS cubemap textures` |
| `S-002` | V | Render the skybox as a named graph pass; camera rotation changes it, translation does not, and resize stays clean | `feat(sky): render a cubemap skybox` |

**M2 exit:** a camera moves through a stable cubemap sky scene; the frame is clean in
the debug layer and readable in a PIX capture.

### M3 - Terrain and materials

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `T-001` | V | Render one deterministic height tile in solid and wireframe modes with inspectable normals/bounds | `feat(terrain): render a heightmapped tile` |
| `S-002A` | V | Replace the diagnostic cubemap's visible RGB with a continuous procedural LDR daylight gradient, soft sun disk/halo, and the same unshadowed ambient-plus-Lambert sun direction on terrain; retain the far-depth sky technique and return immediately to terrain queries | `feat(sky): add procedural daylight and sun` |
| `T-002` | - | Add exact height, normal, bounds, and ray queries; a marker rests on the visible LOD0 triangle surface | `feat(terrain): add canonical terrain queries` |
| `REN-001` | - | Move scene-pass configuration, statistics, and orchestration out of the bootstrap D3D12 presentation class and behind the `Renderer` boundary without changing pixels or smoke accounting | `refactor(render): separate renderer orchestration from D3D12 RHI` |
| `T-003` | V | Blend PBR albedo/normal/roughness texture arrays by slope/height or weights with normal/material debug views | `feat(terrain): add layered PBR materials` |
| `S-003` | V | Add HDR environment conversion plus diffuse/specular IBL and verify it on terrain and one material sphere | `feat(sky): add image-based environment lighting` |
| `T-004` | V | Split the full-resolution terrain into several chunks and add frustum culling with visible bounds/counts | `feat(terrain): add chunk culling` |
| `T-005` | V | Add one coarser derived LOD with crack-free seams and a measured geometric-error bound; collision remains full resolution | `feat(terrain): add bounded terrain LOD` |

**M3 exit:** the user can fly over a textured outdoor terrain whose renderer and
CPU queries agree exactly at LOD0 and stay within the documented visual error at
coarser LODs.

### M4 - Storm and visual water

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `R-001` | V | Add seeded, bounded GPU rain driven by adjustable rate and wind; terrain impacts are correct and particle capacity is fixed | `feat(weather): render wind-driven rain` |
| `R-002` | V | Turn terrain impacts into bounded splash/ripple events with repeatable placement and lifetime | `feat(weather): add rain impact splashes` |
| `R-003` | V | Add a non-conserving visual wetness mask with explicit accumulation and drying controls | `feat(weather): add visual terrain wetness` |
| `R-004` | V | Add rain density/overdraw controls and record GPU timing at low/medium/high quality | `perf(weather): add scalable rain quality` |
| `W-001` | V | Render static water with Fresnel, depth tint/absorption, reflection/refraction approximation, and normal waves | `feat(water): render a visual water surface` |

**M4 exit / Environment Lab 0.1:** sky, textured terrain, rain, wetness, and
visual water run together with measured frame/pass timings.

### M5 - First physics

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `PHY-001` | S | Add fixed 60 Hz scheduling, pause/single-step, gravity, and a collision-free ballistic test that is invariant across render rates | `feat(physics): add deterministic fixed-step motion` |
| `PHY-002` | S | Add sphere-versus-canonical-terrain contact; the sphere rests without hovering or penetration and normals are debug-drawn | `feat(physics): add sphere terrain contact` |
| `PHY-003` | S | Add multiple spheres, brute-force pair generation, normal impulses, and restitution with analytic tests | `feat(physics): add sphere body collisions` |
| `PHY-004` | S | Add orientation, angular velocity, inertia, torque, and angular integration with a torque-response test | `feat(physics): add angular rigid-body state` |
| `PHY-005` | S | Add capsule contacts against terrain, spheres, and capsules with focused closest-feature tests | `feat(physics): add capsule collision` |
| `PHY-006` | S | Add oriented-box SAT contacts and multi-point manifold generation with visual/contact tests | `feat(physics): add box contact manifolds` |
| `PHY-007` | S | Add iterative normal/friction impulses and validate restitution, sliding friction, and slope-rest behavior | `feat(physics): add contact constraint solver` |
| `PHY-008` | S | Add manifold persistence and warm starting; a small crate stack remains stable within tolerance | `feat(physics): stabilize persistent contacts` |
| `PHY-009` | S | Replace brute-force pairs with a verified dynamic AABB tree or sweep-and-prune and expose pair/timing counts | `perf(physics): add collision broad phase` |
| `PHY-010` | S | Add islands and sleeping without changing awake-body results; wake/sleep transitions are test-covered | `perf(physics): add body islands and sleeping` |

### M6 - Fluid simulation

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `W-002` | S | Add a tiny CPU depth/momentum grid, solid boundaries, and a well-balanced lake-at-rest test over uneven terrain | `feat(fluids): add shallow-water reference state` |
| `W-003` | S | Add conservative fluxes and CFL substeps on wet cells; dam-break behavior stays positive and the mass ledger closes | `feat(fluids): advance wet shallow water` |
| `W-004` | S | Add stable dry/wet fronts and shoreline activation without negative depth or unexplained mass loss | `feat(fluids): add wet and dry boundaries` |
| `W-005` | S | Port fixed-step batches to ping-pong compute resources; match CPU cases within tolerance and detect NaN/Inf/negative depth | `feat(fluids): add GPU shallow-water solver` |
| `W-006` | S | Render `terrain height + water depth`; simulated surface normals and velocity drive visual detail/foam | `feat(water): render simulated surface water` |

**M6 exit / Simulation Lab 0.2:** rigid bodies collide with the canonical
terrain while a tested shallow-water state drives the displayed water surface.

### M7 - Coupled environment

| ID | Level | Increment and acceptance gate | Suggested commit |
|---|---:|---|---|
| `C-001` | C | Feed measured rainfall to water; a sealed basin gains the expected volume within an agreed tolerance | `feat(hydrology): couple rainfall to surface water` |
| `H-001` | S | Add terrain-driven infiltration and weather-driven evaporation as explicit boundary sinks; the mass ledger accounts for every removed unit | `feat(hydrology): add water boundary fluxes` |
| `C-002` | S | Add active wet tiles and cross-tile runoff without seams or unexplained mass loss | `feat(hydrology): add tiled terrain runoff` |
| `C-003` | - | Produce a compact, versioned GPU fluid-query/readback ring for body sample points; CPU consumes only completed older data with no frame stall | `feat(fluids): add asynchronous physics queries` |
| `C-004` | C | Apply one-way Archimedes buoyancy and drag from the delayed query snapshot; a test block floats stably across frame rates | `feat(fluids): add body buoyancy and drag` |
| `C-005` | C | Represent static body occupancy as effective moving-bed/free-volume data without deleting water; visualize excluded volume and boundaries | `feat(fluids): add conservative body occupancy` |
| `C-006` | C | Add moving no-penetration boundary fluxes and conservative redistribution; the staggered floating-block case stays stable within mass tolerance | `feat(fluids): couple moving body displacement` |

**M7 exit:** the permanent integration scenario is "rain fills a terrain bowl and
a block floats in it," with conservation, stability, and performance metrics.

## 11. Verification strategy

| Area | Permanent checks |
|---|---|
| Core/build | Fresh configure, Debug/Release build, unit tests, warnings as errors |
| D3D12 | Debug layer clean, focused GPU validation, DRED path, WARP smoke, named PIX passes |
| Camera/cube | Basis and near/far math, elapsed-time input, aspect-changing resize, 24/36 geometry bounds, one static upload, one indexed draw/camera upload/depth clear per submission |
| Sky/assets | Cubemap orientation, translation invariance, sRGB/linear correctness, missing-asset error |
| Terrain | Flat/ramp samples, ray hits, normals, cell/chunk boundary equality, LOD seam captures |
| Rain | Seed repeatability, capacity bounds, emission statistics, impact height, GPU timing |
| Physics | Gravity trajectory, resting contact, slope friction, restitution, stack stability, NaN scan |
| Fluids | Lake at rest, dam break, walls, wet/dry front, non-negative depth, mass accounting, CPU/GPU tolerance |
| Coupling | Sealed-bowl rainfall volume, cross-tile runoff, floating block, displacement conservation |

Image tests and GPU numeric comparisons use documented tolerances because drivers
and floating-point execution differ across GPU vendors. Simulation scenarios use
fixed seeds and record their configuration so failures can be replayed.

## 12. Features deliberately deferred

The following are valuable, but none belongs on the critical path to the coupled
environment:

- editor, scripting, plugins, networking, audio, and character animation;
- custom allocators, a job system, parallel command recording, and async compute;
- arbitrary convex collision, GJK/EPA, continuous collision, destruction, and
  soft bodies;
- procedural atmosphere, volumetric clouds, ocean simulation, erosion, FLIP,
  SPH, and full 3D Navier-Stokes fluids;
- virtual texturing, DirectStorage, mesh-shader terrain, sampler feedback, VRS,
  DXR, and work graphs.

Advanced DirectX features enter through a measured experiment with a baseline
comparison, capability check, fallback policy, PIX capture, and an ADR. "Latest"
means using current stable tools and the best API for a demonstrated problem,
not accumulating feature checkboxes.

### Eventual controllable-entity path

The current `World` boundary deliberately leaves room for controllable entities
without requiring an ECS now. After M7, the likely progression is stable entity
handles and scenario serialization, then a capsule-based character controller
using existing terrain/physics/fluid queries, then glTF skeletal meshes and
animation, and finally behavior or scripting. An ECS is adopted only if measured
entity scale or query patterns make it useful.

## 13. Principal risks and controls

| Risk | Control |
|---|---|
| Scope explosion | One behavior and one acceptance gate per increment; defer volumetric fluid, clouds, erosion, editor, and characters |
| D3D12 lifetime/synchronization bugs | One queue first, centralized graph/barriers, fence retirement, debug layer, GPU validation, DRED |
| Preview API churn | Pin retail Agility/DXC on `main`; experiments stay isolated |
| Hardware differences | Startup capability report, explicit adapter choice, WARP smoke, multi-vendor tolerance tests |
| Render/collision mismatch | One `TerrainData` source and identical cell interpolation rules |
| Custom physics robustness | Restrict shapes/features, build analytic tests, and keep a replaceable backend boundary |
| Fluid instability or lost water | Conservative scheme, CFL substeps, positivity checks, lake-at-rest and mass accounting tests |
| CPU/GPU stalls | GPU-owned fluid state, immutable snapshots, delayed/coarse queries, no full per-frame readback |
| Coupling instability | Progress from independent systems to one-way and then staggered two-way coupling |
| VRAM/descriptor growth | Fixed budgets, fence-safe handles, telemetry before streaming or aliasing |
| Unlicensed content | Procedural/owned/licensed source assets with recorded provenance |

## 14. Immediate next increment

After `S-002A` is reviewed and committed by the owner, return to the roadmap
and implement only `T-002`:

- add exact CPU height and normal sampling against the fixed LOD0 triangle
  split;
- add bounds and ray queries against the same canonical CPU height data;
- place a diagnostic marker on the queried visible LOD0 surface; and
- stop before PBR materials, chunks, LOD, streaming, collision, weather, or
  fluid coupling.

`T-001` begins M3 with a deterministic `33x33` CPU-owned height tile, fixed
cell triangulation, area-weighted presentation normals, solid and wireframe
diagnostics, and an always-present, depth-tested bounds overlay. `S-002A`
temporarily gives that surface simple green/rock daylight shading so the fixed
sun reads as a scene light; it does not add materials or shadows. The terrain
still deliberately lacks the canonical spatial queries assigned to `T-002`;
the renderer-boundary cleanup remains separately reviewable as `REN-001`
before material work begins.

## 15. Primary technical references

- [DirectX 12 Agility SDK releases](https://devblogs.microsoft.com/directx/directx12agility/)
- [Getting started with the Agility SDK](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)
- [Microsoft Direct3D WARP package](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)
- [Microsoft DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex)
- [DirectX Graphics Samples](https://github.com/microsoft/DirectX-Graphics-Samples)
- [D3D12 feature support queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_feature)
- [D3D12 enhanced barriers](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/enhanced-barriers)
- [D3D12 debug-layer interfaces](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-sdklayers-interfaces)
- [DRED device-removal diagnostics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/use-dred)
- [PIX for Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/general/pix-overview)
- [Direct3D 12 timing queries](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)
- [ResolveQueryData](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata)
- [WinPixEventRuntime](https://devblogs.microsoft.com/pix/winpixeventruntime/)
- [D3D12 Work Graphs](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/work-graphs)

This document is the source of truth until specialized ADRs and milestone files
are introduced by the increments that need them.
