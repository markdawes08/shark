# Fixed-Step Simulation and Sphere-Terrain Contact Contract

- **Completed through:** `PHY-002`
- **Last verified:** July 19, 2026

PHY-001 established Shark's fixed-clock ballistic path. PHY-002 gives that one
sphere a one-meter collider and a deterministic contact response against the
canonical LOD0 terrain. This is a focused resting proof, not a general
rigid-body solver.

## Ownership and data flow

The boundaries are intentionally narrow:

- `Simulation` owns fixed-step time accounting, pause state, and step requests;
- `Physics` owns ballistic state, the sphere collider, semi-implicit
  integration, canonical-terrain response, and interpolation;
- `World` publishes the scenario-owned body spawn beside the lake basin;
- the sandbox composition root sequences input, fixed ticks, immutable
  previous/current snapshots, and render interpolation; and
- `Renderer` receives only the interpolated material-sphere world position.

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

## Ballistic body

The Environment Lab scenario places one body at a validated dry position
outside the analytic lake support and above the canonical LOD0 terrain. It
starts with zero linear velocity and uses gravity
`(0, -9.81, 0)` meters per second squared.

For fixed delta `dt = 1/60` second, semi-implicit Euler advances:

```text
velocity = velocity + gravity * dt
position = position + velocity * dt
```

Before each tick, the sandbox copies the current body state to the previous
snapshot and then advances the current snapshot. Rendering linearly
interpolates those two read-only states using the clock alpha. Interpolation
returns a separate value and never modifies either authoritative snapshot.

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

PHY-002 gives the single proof sphere a temporary infinite-friction endpoint
projection: every linear-velocity component becomes zero at contact. That
deliberately feature-limited rule makes the sphere settle on the Environment
Lab terrain without bounce, penetration, or drift; its correction/impulse
magnitude is not a general bounded material law. It is not the later
restitution/friction/contact-constraint model. Each fixed tick republishes an
optional contact containing the untouched canonical sample, pre-resolution
separation, and penetration depth.

## Rendering boundary

The existing material-sphere mesh remains packed in the terrain geometry
buffers at its original authored center. Its vertex shader now reads three
32-bit translation constants from `b2`. The renderer subtracts the authored
center from the interpolated world position and binds that translation for the
single sphere draw in `Terrain`.

This adds no geometry buffer, descriptor, texture, graph pass, water resource,
or upload-buffer allocation. A composition-level contract test validates the
renderer fixture's one-meter visual radius against the scenario-owned
one-meter collider radius.

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
  partitions.

After defining `$cmake` and `$ctest` with the discovery block in
[Building Shark](BUILDING.md#fresh-command-line-build), build and run all
discovered unit cases with:

```powershell
& $cmake --build --preset windows-debug --target SharkTests
& $ctest --preset windows-debug -R '^unit\.'
```

For manual acceptance, launch `SharkSandbox`. The sphere must remain still at
startup, advance by one deterministic amount for each `F6` press, fall after
`F5`, contact the visible terrain, and remain at rest without hover or
penetration. Pause/resume must not create a catch-up jump. Press `F4` and
confirm the cyan preview begins at the fixed canonical support sample and
follows its normal; it is intentionally available before and after contact.
Camera motion and animated presentation-only water remain independent.

The completed PHY-002 validation passed all `161/161` unit cases in both Debug
and Release. The 1,000-frame presentation smoke passed on Debug hardware,
Debug WARP, and Release hardware; each path now fails unless its final
simulation snapshot contains the exact canonical support contact with zero
velocity and one-radius face-plane clearance.

## Explicit non-goals

PHY-002 adds no body/body pair, arbitrary closest-feature sphere/triangle
collision, edge collision when the center is outside the tile, continuous
collision detection, generalized friction/restitution, angular state, torque,
broad phase, sleeping, buoyancy, water displacement, reset control, entity
system, or general debug-draw service. Its one-sample face support is intended
for the current one-meter-radius, four-meter-cell, slope-bounded Environment
Lab heightfield proof; it is not a general promise for arbitrary radius/cell
ratios or near-vertical faces. The lake remains W-001
presentation-only water, and `R-001` through `R-004` remain deferred. The
active queue is centralized in [ENGINE_PLAN.md](ENGINE_PLAN.md).
