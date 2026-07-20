# Fixed-Step Sphere Simulation Contract

- **Completed through:** `PHY-003`
- **Last verified:** July 19, 2026

PHY-001 established Shark's fixed-clock ballistic path. PHY-002 gives that one
sphere a one-meter collider and deterministic canonical-terrain support.
PHY-003 expands the Environment Lab to a stable fixed array of four equal-unit-
mass spheres and resolves their overlaps with restitution impulses. This is a
bounded discrete collision proof, not a general rigid-body solver.

## Ownership and data flow

The boundaries are intentionally narrow:

- `Simulation` owns fixed-step time accounting, pause state, and step requests;
- `Physics` owns the fixed four-body capacity, ballistic state, colliders,
  semi-implicit integration, canonical-terrain response, sphere-pair response,
  contact records, and interpolation;
- `World` publishes four deterministic scenario-owned body spawns and initial
  velocities beside the lake basin;
- the sandbox composition root sequences input, fixed ticks, immutable
  previous/current snapshots, and render interpolation; and
- `Renderer` receives only an active count and four interpolated sphere
  positions.

Neither the clock nor ballistic body includes Win32 or Direct3D types. The
platform continues to publish raw events, and the renderer cannot advance or
mutate simulation state. Physics depends on the platform-independent
`HeightTileSurface` query contract; Terrain has no dependency on Physics.

## Fixed clock

The production clock runs at exactly 60 ticks per second. It accumulates
elapsed render time in an integer nanosecond phase, emits zero or more fixed
steps, and publishes an interpolation alpha in `[0, 1]`. One render update
accepts at most 250 milliseconds; any excess is reported as discarded time
instead of causing an unbounded catch-up.

The interactive sandbox starts paused. Paused frames discard wall-clock elapsed
time and render the current snapshot with alpha `1`. Changing pause state clears
the partial accumulator, so time spent paused cannot become a later burst of
simulation work. The deterministic presentation-smoke path instead runs from
startup and supplies one fixed-step ceiling duration per frame.

| Input | Simulation action |
|---|---|
| `F5` | Toggle pause/resume on a non-repeat key press |
| `F6` | While paused, request exactly one 60 Hz tick |

A single-step request is consumed once. It does not resume the clock or carry
render-frame elapsed time into the simulation.

## Sphere body snapshots

The Environment Lab publishes an exact active count of four, all with a
one-meter radius. Body 0 remains the original zero-velocity terrain-rest proof.
Bodies 1 and 2 start at the same airborne Y coordinate, 12 meters apart, with
X velocities of `+5` and `-3` meters per second. Body 3 is an isolated
zero-velocity fall. Every body uses gravity `(0, -9.81, 0)` meters per second
squared.

For fixed delta `dt = 1/60` second, semi-implicit Euler advances:

```text
velocity = velocity + gravity * dt
position = position + velocity * dt
```

Before each tick, the sandbox copies the entire fixed current-state array to
the previous-state array. It advances the active current states in stable body
index order, resolves pairs, and publishes no partial state. Rendering
interpolates each previous/current pair using the clock alpha and never mutates
either authoritative snapshot.

## Canonical terrain contact

After each ballistic prediction, Physics samples
`HeightTileSurface::sample_lod0_surface` exactly once at the predicted center's
X/Z position. The returned position, exact geometric triangle normal, cell,
triangle, and barycentrics are the contact source. Smooth render normals,
coarse visual LOD, render meshes, and GPU resources never participate.

For predicted center `C`, canonical point `P`, unit face normal `N`, and radius
`r`, the signed sphere/plane separation is:

```text
separation = dot(C - P, N) - r
```

A separation above the 0.00001-meter contact tolerance remains ballistic. At
or below the tolerance, the solver changes only center Y:

```text
supportedCenterY = P.y + r / N.y
```

This places the sphere exactly one radius from the selected triangle plane
without changing X/Z and therefore without switching query ownership during
resolution. The corrected state is committed only after every calculation
succeeds. A center outside the tile continues ballistically with no contact;
the maximum X/Z edges retain the terrain query's inclusive ownership.

The terrain endpoint still gives each contacting sphere a temporary
infinite-friction endpoint
projection: every linear-velocity component becomes zero at contact. That
deliberately feature-limited rule makes the sphere settle on the Environment
Lab terrain without bounce, penetration, or drift; its correction/impulse
magnitude is not a general bounded material law. It is not the later
restitution/friction/contact-constraint model. Each fixed tick republishes an
optional contact containing the untouched canonical sample, pre-resolution
separation, and penetration depth.

## Sphere-pair collision

After every active sphere has completed ballistic prediction and terrain
support, Physics tests the active pairs exactly once in lexicographic order:

```text
(0,1), (0,2), (0,3), (1,2), (1,3), (2,3)
```

The fixed capacity is four bodies and six possible pairs. There is no dynamic
allocation and no broad phase. For centers `A` and `B`, radii `rA` and `rB`,
and first-to-second normal `N`:

```text
offset      = B - A
overlap     = rA + rB - length(offset)
N           = normalize(offset)
```

Pairs with negative overlap are separated. Touching and overlapping pairs
publish a contact. Coincident centers use `+X` as a deterministic,
index-ordered fallback normal. Positive overlap is projected equally between
the equal-unit-mass bodies:

```text
A = A - N * overlap / 2
B = B + N * overlap / 2
```

With relative velocity `Vrel = Vb - Va`, an impulse is applied only when
`dot(Vrel, N) < 0`:

```text
impulse = -(1 + restitution) * dot(Vrel, N) / 2
Va      = Va - N * impulse
Vb      = Vb + N * impulse
```

The Environment Lab restitution is `0.75`. Tangential velocity is untouched,
and equal-unit-mass linear momentum is preserved. Each contact records stable
body indices, normal, penetration, incoming relative normal velocity, and
normal-impulse magnitude. Invalid counts, states, radii, restitution, or
non-representable results fail transactionally without changing the input
array.

The first Environment Lab pair collision is deliberately airborne. Therefore
the temporary terrain-sticking response cannot erase its proof impulse during
that tick, while isolated body 0 continues to prove canonical support. Pair
projection is a single discrete pass; it does not iterate a stack to
convergence and it cannot prevent fast bodies from tunneling.

## Rendering boundary

The existing material-sphere mesh remains packed in the terrain geometry
buffers at its original authored center. Its vertex shader now reads three
32-bit translation constants from `b2`. `RenderFrameData` owns a fixed
four-position array and an active count in `[0, 4]`. The renderer binds the
material-sphere PSO once, then visits active positions in index order, rebinds
`b2`, and issues the existing indexed draw once per sphere inside `Terrain`.

This adds no geometry buffer, descriptor, texture, graph pass, water resource,
or upload-buffer allocation. Four active bodies produce exactly four draws and
`4 * 1,584` indices per submitted frame. Composition-level tests lock the
physics, world, and renderer capacities together and validate the renderer
fixture's one-meter visual radius against the scenario-owned collider radius.

`F4` retains the existing bounded six-vertex/six-index cyan normal pin and
magenta visible-chunk bounds. The pin is now built at the proof sphere's fixed
support sample and extends two meters from the canonical surface along the
exact face normal, so its tip remains visible after the sphere settles. It is
a support-normal preview, not an active-contact indicator: `F4` also shows it
while the sphere is airborne. No per-frame diagnostic upload or additional
draw was added.

## Verification

Permanent CPU coverage checks:

- exact 60 Hz step accounting under differently partitioned render deltas;
- pause, resume, accumulator reset, and one-shot single-step behavior;
- bounded elapsed-time acceptance and invalid-input rejection;
- semi-implicit ballistic motion under standard gravity;
- the same authoritative trajectory across different render rates;
- interpolation endpoints, intermediate values, and finite-state validation;
- flat-terrain fall, exact long-term support, and zero resting velocity;
- exact sloped-face plane clearance and geometric normal ownership;
- diagonal, maximum-edge, immediately-outside, and separating transitions;
- transactional rejection of invalid radii, state, deltas, and overflow; and
- bit-identical resting state/contact count across 30/60/120/144 Hz render
  partitions;
- fixed four-body/six-pair capacity and lexicographic pair traversal;
- separated no-op, equal overlap projection, touching, and coincident-center
  fallback behavior;
- equal-mass restitution, unequal incoming speeds, tangential preservation,
  momentum conservation, and separating-overlap behavior;
- transactional count, restitution, state, collider, and numeric-overflow
  rejection;
- bit-identical sphere-collision results across 30/60/120/144 Hz render
  partitions; and
- the real Environment Lab pair impulse occurring before either pair body
  touches terrain while body 0 reaches canonical support.

After defining `$cmake` and `$ctest` with the discovery block in
[Building Shark](BUILDING.md#fresh-command-line-build), build and run all
discovered unit cases with:

```powershell
& $cmake --build --preset windows-debug --target SharkTests
& $ctest --preset windows-debug -R '^unit\.'
```

For manual acceptance, launch `SharkSandbox`. Four spheres must remain still
at startup. `F6` advances all four by one deterministic tick. After `F5`,
bodies 1 and 2 collide while airborne and separate, while body 0 falls to the
visible terrain and remains at rest without hover or penetration. Pause/resume
must not create a catch-up jump. `F4` still previews only body 0's fixed
canonical support normal and is intentionally available before and after
contact. Camera motion and presentation-only water remain independent.

PHY-003's focused collision suite passes `3,662` assertions across 12 cases in
both Debug and Release. Both full unit suites pass `173/173`. The presentation
smoke passes 1,000 frames on Debug hardware, 600 frames on Debug WARP, and
1,000 frames on Release hardware. Every path observes the airborne `(1,2)`
impulse and retains body 0's exact canonical support; they record exactly
4,000, 2,400, and 4,000 sphere draws respectively, with no D3D12 corruption,
errors, or live child objects.

## Explicit non-goals

PHY-003 adds no unequal mass, angular state, torque, friction, persistent
manifold, iterative constraint solve, continuous collision detection, broad
phase, sleeping, arbitrary closest-feature sphere/triangle collision,
buoyancy, water displacement, reset control, entity system, or general
debug-draw service. Its one-sample face support remains intended for the
current one-meter-radius, four-meter-cell, slope-bounded Environment Lab
heightfield proof. The lake remains W-001 presentation-only water, and `R-001`
through `R-004` remain deferred. The active queue is `PHY-004` angular
rigid-body state and is centralized in [ENGINE_PLAN.md](ENGINE_PLAN.md).
