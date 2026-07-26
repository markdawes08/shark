# Character Contract

- **Completed through:** `CHR-001`
- **Last verified:** July 25, 2026

CHR-001 establishes one deterministic, bounded player capsule for the Island
Demo. It creates the authority, input, snapshot, spawn/reset, and presentation
boundaries needed by later character work. It deliberately does not add
gravity, grounding, locomotion, jumping, wading, swimming, an entity registry,
or final avatar art.

## Ownership and data flow

`engine/character` owns the platform- and renderer-independent capsule
contract. It depends only on Core. The sandbox composition root translates
Win32-derived platform events into `PlayerActionCommand`, advances Character
once per emitted fixed tick, interpolates the published snapshots, and maps the
result to a renderer-only proxy:

```text
Platform events
  -> sandbox PlayerCommandSource
  -> one PlayerActionCommand per emitted 60 Hz tick
  -> Character previous/current snapshots
  -> presentation-only interpolation
  -> renderer DebugCapsuleProxy
```

Character never reads platform key codes, a render mesh, D3D12 state, the
camera, terrain, water, or dynamic Physics state. The renderer never advances
or mutates Character.

## Capsule and Island Demo spawn

The capsule is upright along world `+Y`. Its shape is a radius plus the
half-length of the centerline segment, so its total vertical extent from its
center is:

```text
radius + vertical_half_segment
```

The Island Demo authors both values as `0.5` meter. The resulting capsule is
one meter wide and two meters tall. Its canonical dry-spawn values are:

```text
ground:  (0, 0.890625, 112)
center:  (0, 1.890625, 112)
yaw:     0 radians
```

The capsule bottom exactly equals the canonical LOD0 terrain sample. The
scenario proves that the same X/Z point returns `no_water` from WQ-001. Center
position is bounded to the resident terrain footprint after shrinking X/Z by
the capsule radius, and to `[-32, 64]` meters vertically.

Creation rejects nonfinite data, unordered bounds, an out-of-bounds spawn, and
shape dimensions outside `(0, 4]` meters. Successful creation canonicalizes
signed zero and wraps yaw to `[-pi, pi)`.

## Fixed-tick commands

The sandbox currently maps:

| Input | Character action |
|---|---|
| `W`, `S`, `A`, `D` | forward, backward, left, right held |
| either `Shift` | run held |
| `Space` | jump pressed |
| left mouse press | primary action pressed |
| right-mouse drag | bounded yaw/pitch look deltas |
| `R` | reset pressed |

Held actions repeat on every emitted fixed tick. Jump, primary action, reset,
and accumulated look are consumed once by the first emitted tick after their
events. Zero-tick render frames do not consume pending input, while a catch-up
frame samples separately for each emitted tick. Focus loss, minimization,
closure, an event-buffer overflow, or an explicit source reset clears held and
pending input so a key cannot remain stuck.

CHR-001 records every valid command in the authoritative snapshot. Only
`reset_pressed` changes pose in this increment; movement, look, jump, and
primary action become behavior in later increments.

## Snapshot and reset rules

`advance_player_capsule` accepts only the exact next fixed-tick number. It
validates both source and candidate state before committing, so a rejected
advance leaves the simulation byte-for-byte unchanged.

An ordinary tick publishes the old current snapshot as previous and records
the new command and tick as current. A reset restores the authored spawn,
increments the reset generation, and collapses both snapshot poses to that
spawn while publishing ticks `N-1` and `N`. Presentation interpolation
therefore cannot sweep through a reset teleport.

`interpolate_player_capsule` is read-only. It linearly interpolates position
and follows the shortest wrapped yaw arc for alpha in `[0, 1]`.

## Temporary presentation proxy

The visible player is a blue diagnostic capsule. The renderer reuses the
existing 266-vertex/1,584-index material-sphere geometry and its Terrain pass,
PSO, buffers, descriptors, and lighting path. Capsule deformation happens in
the existing material-sphere shader. An enabled proxy adds exactly one indexed
draw per submitted frame and no graph pass, GPU resource, descriptor,
allocation, upload, or timestamp interval.

The ordinary sandbox camera starts at `(0, 4.890625, 122)` with pitch `-0.28`
radians so the spawn proxy can be inspected. It remains an independently
movable free-fly camera rather than a player-follow camera until `CAM-001`.

## Verification

Permanent tests cover:

- configuration, state, command, tick, interpolation, overflow, and
  transactional rollback boundaries;
- exact dry-spawn placement and WQ-001 agreement;
- held, pulse, look, repeat, lifecycle, and overflow-safe input sampling;
- an exact 120-command/snapshot transcript across 30, 60, 120, and 144 Hz
  render partitions, including zero-tick and multi-tick render frames;
- CPU/HLSL capsule deformation expectations, root-constant layout, renderer
  validation, and enabled/disabled accounting; and
- one capsule draw and 1,584 capsule indices per submitted smoke frame while
  all existing render-graph and resource counts remain unchanged.

The complete Debug and Release suites each pass 497,863 assertions across 351
cases. The final 1,000-frame Debug RTX 4070 presentation smoke records 1,000
capsule draws, 5,000 unchanged graph-pass executions, zero D3D12
corruption/errors, and zero live D3D12 child objects.

Launch `out\build\windows-vs2026\bin\Debug\SharkSandbox.exe` to inspect the
capsule. `R` resets it; the other authored character commands are visible only
to the fixed-tick contract until locomotion is implemented.

The next increment is `CAM-001`: an interpolated third-person follow/orbit
camera with bounded orbit controls and a canonical-terrain obstruction probe.
