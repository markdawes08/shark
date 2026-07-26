# Character Contract

- **Completed through:** `CHR-004`
- **Camera integration verified through:** `CAM-001`
- **Last verified:** July 26, 2026

CHR-004 turns the Island Demo's single bounded player capsule into a
deterministic land character controller. Camera-relative walk/run and
airborne intent, authoritative horizontal and vertical velocity, acceleration,
braking, facing, bounded terrain traversal, jump launch, rising, falling,
landing, recovery, and stable support all advance at the fixed simulation
rate.

Water movement, obstacle stepping, arbitrary world collision, an entity
registry, and final avatar art remain outside this increment.

## Ownership and data flow

`engine/character` owns the platform- and renderer-independent controller. It
depends on Core and Terrain, using only `HeightTileSurface` CPU queries. It
does not enter the dynamic rigid-body arrays or read a render mesh, coarse
visual LOD, D3D12 resource, camera, or gameplay-water result.

```text
Platform events
  -> sandbox PlayerCommandSource
  -> one PlayerActionCommand per emitted 60 Hz tick
  -> World advances the authoritative orbit and publishes a horizontal basis
  -> Character maps ground/air intent, velocity, facing, and terrain probes
  -> Character previous/current snapshots
  -> presentation-only player interpolation
  -> renderer DebugCapsuleProxy
  -> World third-person target (read-only sibling consumer)
```

The sandbox advances the third-person orbit first on each emitted fixed tick,
derives a Character-owned movement frame from that tick's new yaw, advances
Character on the same tick number, and then advances the retained dynamic
sphere simulation. A simultaneous look-and-move command therefore uses the
new heading without a one-tick lag. Character does not depend on World; the
composition root copies World's basis into `PlayerMovementFrame`. Free-fly
camera mode neutralizes character actions but does not suspend gravity,
airborne momentum, or grounding.

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

This remains deliberately a heightfield controller. CHR-004 samples the full
three-dimensional capsule-center path at bounded intervals, but does not
perform exact continuous collision detection, an exact crossed-triangle
query, or a swept-capsule query against side features, overhangs, props, or
arbitrary obstacles. A sufficiently narrow or corner-grazed non-walkable face
on a different terrain topology can fall between samples; the Island Demo's
authored topology and route are verified separately.

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
| `rising` | A grounded launch or continued ascent has positive post-gravity vertical velocity; there is no active support normal |
| `falling` | Gravity advances finite nonpositive vertical velocity; there is no active support normal |
| `landing` | This descending tick crossed onto walkable support; position is exact and vertical velocity is positive zero |
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

When a descending center-path probe crosses support plus the snap distance,
Character publishes the exact support height and positive-zero vertical
velocity. The sampled crossing protects the authored heightfield path without
claiming exact continuous collision detection. A non-walkable crossing
publishes `steep_contact`, not `grounded`.

## Ground locomotion

The Island Demo publishes this bounded `PlayerGroundLocomotionSettings`
record:

| Setting | Island Demo value |
|---|---:|
| walk target | `4 m/s` |
| run target | `7 m/s` |
| acceleration | `24 m/s^2` |
| braking deceleration | `32 m/s^2` |
| facing turn speed | `10 rad/s` |
| maximum terrain-probe spacing | `0.25 m` |

Each fixed tick cancels opposing held directions, combines the signed
forward/right axes through the current camera-relative movement frame, and
normalizes a nonzero diagonal. This prevents diagonal input from exceeding
the selected walk or run target.

The authoritative horizontal velocity moves toward the requested vector by a
bounded amount. Acceleration is used while gaining speed; braking is used
with no intent, while reversing, or while dropping toward a lower target.
Braking snaps exactly to positive zero rather than oscillating across rest.
Facing turns toward meaningful input through the shortest wrapped yaw arc,
bounded by the authored turn speed. Facing is retained when input is neutral.

Ordinary ground displacement uses the new velocity semi-implicitly. Character
divides the requested path into deterministic probes no farther apart than
the authored maximum. Every accepted probe must have a walkable canonical
LOD0 support sample and a slope-correct center inside the configured bounds.
A steep, missing, or out-of-bounds probe stops traversal, retains the last
safe probe, and zeros horizontal velocity. This is sampled rejection rather
than a continuous sweep.

## Jumping and airborne control

The Island Demo publishes this bounded `PlayerAirLocomotionSettings` record:

| Setting | Island Demo value |
|---|---:|
| pre-gravity jump launch speed | `6.5 m/s` |
| airborne control acceleration | `12 m/s^2` |

`Space` launches only from `grounded` or the one-tick `landing` phase. Reset
wins if reset and jump arrive together. On a launch tick, Character applies
airborne horizontal control rather than the stronger ground acceleration,
sets pre-gravity vertical velocity to `6.5 m/s`, and then performs the same
semi-implicit gravity step:

```text
velocity_y = 6.5 - 9.81 * fixed_delta
center_y  += velocity_y * fixed_delta
```

Airborne intent uses the same newly advanced, camera-relative movement frame
as grounded movement and the same `4/7 m/s` walk/run targets. Horizontal
velocity moves toward a nonzero target by at most
`12 m/s^2 * fixed_delta`, weaker than the `24 m/s^2` ground acceleration.
With neutral airborne input, horizontal momentum is preserved exactly: there
is no implicit air drag or braking. Meaningful intent continues to turn
facing through the same bounded shortest-arc rule.

Jump pulses received during `rising` or `falling` are consumed as one-tick
commands but ignored by the controller, so there is no double jump. The
post-gravity vertical velocity selects `rising` while positive and `falling`
once nonpositive.

Airborne movement samples the full three-dimensional center path with probes
no farther than `0.25 m` apart. The first descending walkable crossing lands
at exact slope-correct support, retains horizontal velocity, and publishes
exactly one `landing` tick. The next supported tick is `grounded` unless a new
jump launches directly from `landing`.

A rising terrain intrusion or rejected X/Z bound keeps the last safe
horizontal prefix, zeros horizontal velocity, and continues vertical motion.
A steep crossing also zeros horizontal velocity. Missing terrain support is
legal while airborne; if the uncontacted capsule falls below its configured
minimum Y, Character recovers to the canonical dry spawn. These are
deterministic sampled heightfield rules, explicitly not an exact swept capsule
or continuous collision detector.

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
generation, zeros horizontal and vertical velocity, and collapses
previous/current pose and motion state. Falling below the configured minimum Y
uses the same recovery path and also increments the reset generation. Neither
recovery emits a synthetic landing pulse or interpolates through the
teleport.

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

CHR-004 consumes horizontal movement, run, jump, and reset while always
advancing gravity and grounding. Primary action remains deterministic command
data for a later increment. Character still does not query WQ-001 or apply
water movement policy; that boundary begins at CHR-005.

## Temporary presentation proxy

The visible player remains the blue diagnostic capsule. The renderer reuses
the existing material-sphere geometry and Terrain pass. Capsule deformation
happens in the existing shader, so the proxy adds one indexed draw per
submitted frame and no graph pass, GPU resource, descriptor, allocation,
upload, or timestamp interval.

The default World-owned third-person rig targets the same interpolated player
position. Rising and falling therefore move the proxy and camera target
together. Terrain obstruction can shorten only the presentation boom and
cannot mutate Character. `F7` retains the independent free-fly diagnostic
camera.

## Verification

Permanent tests cover:

- camera-relative walk/run caps, diagonal normalization, opposing-input
  cancellation, acceleration, braking, exact rest, and reversal;
- bounded shortest-arc facing and same-tick look-plus-move behavior;
- gentle-slope traversal with exact support, safe-prefix rejection at steep
  terrain and center bounds, and the complete eight-point dry Island loop;
- `6.5 m/s` launch integration, rising/apex/falling phase changes, ignored
  airborne jump pulses, and direct relaunch from the one-tick landing phase;
- weaker camera-relative airborne steering, exact neutral horizontal momentum
  preservation, and same-tick look-plus-air-move behavior;
- sampled three-dimensional terrain paths, rising intrusion/bounds rejection,
  descending exact support, and landing-to-ground movement;
- flat support over hundreds of ticks with exact positive-zero velocity;
- analytic semi-implicit falling, exactly one landing tick, and stable
  grounded recovery;
- gentle, threshold-equal, and steep face classification with slope-correct
  plane separation;
- canonical diagonal/edge ownership and explicit outside-terrain misses;
- airborne and supported reset behavior, below-minimum-Y recovery, reset
  generation, and interpolation-history collapse;
- malformed settings, delta, state, tick, overflow, bounds, and source-terrain
  disagreement with transactional rollback;
- exact command, camera basis, pose, horizontal/vertical velocity, phase,
  support, and reset transcripts across 30, 60, 120, and 144 Hz render
  partitions;
- moving player/camera presentation composition; and
- unchanged render-graph, resource, and one-capsule-draw smoke accounting.

The complete Debug and Release suites each pass 564,929 assertions across 392
cases, including exact 30/60/120/144 Hz transcripts. The neutral RTX 4070
presentation smoke passes 1,000 grounded ticks, 1,000 capsule draws, and 5,000
graph passes. Packaged WARP passes 600 grounded ticks, 600 capsule draws, and
3,000 graph passes. Both finish with zero D3D12 corruption/errors and zero live
D3D12 child objects; CHR-004 adds no render pass or GPU resource.

Launch `out\build\windows-vs2026\bin\Debug\SharkSandbox.exe` to inspect the
grounded capsule. Press `F5` to resume/pause fixed-tick simulation, use
`W`/`A`/`S`/`D` to walk and either `Shift` to run, `F6` to single-step while
paused, `Space` to jump, right-drag/wheel to orbit and zoom, `F7` for free-fly
diagnostics, and `R` for a canonical grounded reset.

The next increment is `CHR-005`: shallow-water wading through the existing
WQ-001 CPU query, with water remaining non-authoritative to Character until
that increment.
