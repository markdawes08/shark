# Character Contract

- **Completed through:** `CHR-002`
- **Camera integration verified through:** `CAM-001`
- **Last verified:** July 26, 2026

CHR-002 turns the Island Demo's single bounded player capsule into a
deterministic vertical character controller. Standard gravity, canonical
terrain support, walkable-slope classification, falling, one-tick landing, and
stable grounded state now advance at the fixed simulation rate.

Horizontal locomotion, jumping, water movement, obstacle stepping, arbitrary
world collision, an entity registry, and final avatar art remain outside this
increment.

## Ownership and data flow

`engine/character` owns the platform- and renderer-independent controller. It
depends on Core and Terrain, using only `HeightTileSurface` CPU queries. It
does not enter the dynamic rigid-body arrays or read a render mesh, coarse
visual LOD, D3D12 resource, camera, or gameplay-water result.

```text
Platform events
  -> sandbox PlayerCommandSource
  -> one PlayerActionCommand per emitted 60 Hz tick
  -> Character queries canonical LOD0 terrain at player X/Z
  -> Character previous/current snapshots
  -> presentation-only player interpolation
  -> renderer DebugCapsuleProxy
  -> World third-person target (read-only sibling consumer)
```

The sandbox advances Character first on each emitted fixed tick, then the
third-person orbit rig on the same tick number, followed by the retained
dynamic sphere simulation. Free-fly camera mode neutralizes character actions
but does not suspend gravity or grounding.

## Capsule and support model

The capsule is upright along world `+Y`. Its shape is a radius plus the
half-length of its vertical centerline segment. The Island Demo authors both
as `0.5` meter, producing a one-meter-wide, two-meter-tall capsule.

Ground support samples the exact canonical LOD0 face at the capsule center's
X/Z. Given sampled point `p`, exact geometric face normal `n`, capsule radius
`r`, and half-segment `h`, the supported center is:

```text
center_y = p.y + h + r / n.y
```

This places the lower spherical cap one radius from the selected face plane;
using only `p.y + h + r` would penetrate a sloped face. The sample retains
Terrain's fixed triangle split and inclusive maximum-edge ownership. Render
normals and coarse terrain never participate.

This is deliberately a one-X/Z-sample heightfield controller. It does not yet
perform a swept capsule query against side features, overhangs, props, or
arbitrary obstacles. Horizontal sweep, bounded slope traversal, and movement
rejection belong to `CHR-003`.

## Grounding policy and states

The scenario publishes a bounded `PlayerGroundingSettings` record:

| Setting | Island Demo value |
|---|---:|
| gravity magnitude | `9.81 m/s²` |
| minimum walkable normal Y | `0.70710678` (inclusive 45-degree limit) |
| ground snap distance | `0.05 m` |

A fixed delta must be finite and in `(0, 0.25]` seconds. The ordinary sandbox
uses the existing fixed 60 Hz clock.

Every authoritative snapshot contains vertical velocity, an exact support
normal when touching terrain, and one explicit phase:

| Phase | Meaning |
|---|---|
| `falling` | Gravity advances finite nonpositive vertical velocity; there is no active support normal |
| `landing` | This tick crossed onto walkable support; position is exact and velocity is positive zero |
| `grounded` | Walkable support remains exact and stable; no gravity jitter is accumulated |
| `steep_contact` | Terrain contact is too steep to call walkable; vertical penetration is blocked while slope traversal remains deferred |

Walkable classification is the inclusive comparison
`surface.normal.y >= minimum_walkable_normal_y`. A landing phase lasts exactly
one authoritative tick and becomes grounded on the next supported tick.

Airborne motion uses semi-implicit Euler:

```text
velocity_y -= gravity_magnitude * fixed_delta
center_y   += velocity_y * fixed_delta
```

When the predicted center crosses support plus the snap distance, Character
publishes the exact support height and positive-zero velocity. The crossing
test prevents discrete downward tunneling through the selected heightfield
sample. A non-walkable crossing publishes `steep_contact`, not `grounded`.

## Spawn, reset, and interpolation

Creation validates and canonicalizes shape, bounds, grounding policy, signed
zero, and yaw. It queries the authored spawn against canonical terrain before
publishing tick zero. A spawn within the bounded snap distance is stored at
the exact support height and starts grounded (or in steep contact); a higher
spawn starts falling. A spawn meaningfully below support is rejected.

The Island Demo spawn remains dry at `(0, 0.890625, 112)` on canonical terrain
and returns `no_water` from WQ-001. Its capsule center is derived from the
support formula rather than duplicated scenario arithmetic.

`advance_player_capsule` accepts only the exact next fixed-tick number. It
validates source terrain consistency, command, delta, arithmetic, bounds, and
the complete candidate before committing. A rejected call leaves the whole
simulation unchanged.

`R` re-queries and restores the canonical authored spawn, increments the reset
generation, and collapses previous/current pose and vertical state. Reset
never emits a synthetic landing pulse or interpolates through the teleport.

`interpolate_player_capsule` remains presentation-only: position is linear and
yaw follows the shortest wrapped arc. Vertical phase and velocity stay
authoritative snapshot data. F5, minimize/restore, dropped-event recovery, and
other time-baseline discontinuities also collapse the player's interpolation
interval alongside the camera and dynamic spheres, preventing a one-tick
visual snap when simulation resumes.

## Commands and current controls

The sandbox maps:

| Input | Character action |
|---|---|
| `W`, `S`, `A`, `D` | forward, backward, left, right held |
| either `Shift` | run held |
| `Space` | jump pressed |
| left mouse press | primary action pressed |
| right-mouse drag | bounded yaw/pitch look deltas |
| `R` | reset pressed |

Held actions repeat on emitted fixed ticks. Jump, primary action, reset, and
accumulated look are one-tick pulses. Zero-tick render frames do not consume
pending input; catch-up frames sample separately for each emitted tick.

CHR-002 consumes reset and always advances vertical grounding. Horizontal,
run, jump, and primary-action fields remain deterministic command data for
later increments.

## Temporary presentation proxy

The visible player remains the blue diagnostic capsule. The renderer reuses
the existing material-sphere geometry and Terrain pass. Capsule deformation
happens in the existing shader, so the proxy adds one indexed draw per
submitted frame and no graph pass, GPU resource, descriptor, allocation,
upload, or timestamp interval.

The default World-owned third-person rig targets the same interpolated player
position. Falling therefore moves the proxy and camera target together.
Terrain obstruction can shorten only the presentation boom and cannot mutate
Character. `F7` retains the independent free-fly diagnostic camera.

## Verification

Permanent tests cover:

- flat support over hundreds of ticks with exact positive-zero velocity;
- analytic semi-implicit falling, exactly one landing tick, and stable
  grounded recovery;
- gentle, threshold-equal, and steep face classification with slope-correct
  plane separation;
- canonical diagonal/edge ownership and explicit outside-terrain misses;
- airborne and supported reset behavior plus interpolation-history collapse;
- malformed settings, delta, state, tick, overflow, bounds, and source-terrain
  disagreement with transactional rollback;
- exact command, pose, velocity, phase, support, and reset transcripts across
  30, 60, 120, and 144 Hz render partitions;
- vertical player/camera presentation composition; and
- unchanged render-graph, resource, and one-capsule-draw smoke accounting.

The complete Debug and Release suites each pass 506,735 assertions across 378
cases. The final 1,000-frame Debug RTX 4070 presentation smoke records 1,000
canonically grounded player ticks, 1,000 capsule draws, 5,000 unchanged graph
passes, zero D3D12 corruption/errors, and zero live D3D12 child objects.

Launch `out\build\windows-vs2026\bin\Debug\SharkSandbox.exe` to inspect the
grounded capsule. Press `F5` to resume/pause fixed-tick simulation, `F6` to
single-step while paused, right-drag/wheel to orbit and zoom, `F7` for
free-fly diagnostics, and `R` for a canonical grounded reset.

The next increment is `CHR-003`: fixed-tick walk/run acceleration, braking,
facing, and bounded terrain-slope traversal from camera-relative commands.
