# Fixed-Step Simulation and Ballistic Motion Contract

- **Completed through:** `PHY-001`
- **Last verified:** July 19, 2026
- **Next increment:** `PHY-002` - sphere contact with canonical terrain

PHY-001 establishes Shark's first simulation path: one collision-free body
advances under gravity on a fixed clock while rendering remains free to run at
a different rate. This is a deliberately small foundation for later terrain
contact, not a general rigid-body engine.

## Ownership and data flow

The boundaries are intentionally narrow:

- `Simulation` owns fixed-step time accounting, pause state, and step requests;
- `Physics` owns the ballistic body state, semi-implicit integration, and
  interpolation operation;
- `World` publishes the scenario-owned body spawn beside the lake basin;
- the sandbox composition root sequences input, fixed ticks, immutable
  previous/current snapshots, and render interpolation; and
- `Renderer` receives only the interpolated material-sphere world position.

Neither the clock nor ballistic body includes Win32 or Direct3D types. The
platform continues to publish raw events, and the renderer cannot advance or
mutate simulation state.

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

PHY-001 intentionally has no collision. The sphere will pass through terrain
and the presentation-only lake after enough steps; that visible behavior is
not a contact failure. `PHY-002` replaces it with the first canonical-terrain
contact proof.

## Rendering boundary

The existing material-sphere mesh remains packed in the terrain geometry
buffers at its original authored center. Its vertex shader now reads three
32-bit translation constants from `b2`. The renderer subtracts the authored
center from the interpolated world position and binds that translation for the
single sphere draw in `Terrain`.

This adds no geometry buffer, descriptor, texture, graph pass, water resource,
or upload-buffer allocation for the transform. The same sphere remains the HDR
lighting proof; only its world translation changes.

## Verification

Permanent CPU coverage checks:

- exact 60 Hz step accounting under differently partitioned render deltas;
- pause, resume, accumulator reset, and one-shot single-step behavior;
- bounded elapsed-time acceptance and invalid-input rejection;
- semi-implicit ballistic motion under standard gravity;
- the same authoritative trajectory across different render rates; and
- interpolation endpoints, intermediate values, and finite-state validation.

After defining `$cmake` and `$ctest` with the discovery block in
[Building Shark](BUILDING.md#fresh-command-line-build), build and run all
discovered unit cases with:

```powershell
& $cmake --build --preset windows-debug --target SharkTests
& $ctest --preset windows-debug -R '^unit\.'
```

For manual acceptance, launch `SharkSandbox`. The sphere must remain still at
startup, advance by one small deterministic amount for each `F6` press, fall
continuously after `F5`, and freeze without a catch-up jump when `F5` pauses it
again. Camera motion and the animated visual-water surface remain independent.

## Explicit non-goals

PHY-001 adds no collider, terrain contact, resting constraint, body/body pair,
angular state, torque, friction, restitution, broad phase, sleeping, buoyancy,
water displacement, reset control, entity system, or general action map.
The lake remains W-001 presentation-only water, and `R-001` through `R-004`
remain deferred. These limits preserve the approved San Andreas-class product
scope while establishing modern deterministic simulation timing.
