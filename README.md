# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

The latest completed increment is `S-002A`, a short visual diversion after
`T-001`. A continuous procedural daylight sky now supplies a horizon, zenith,
lower-sky haze, and fixed sun, while the same sun direction gives the
deterministic `33x33` terrain simple ambient-plus-Lambert lighting. The
translation-invariant far-depth sky technique and terrain diagnostics remain
intact; work returns to the exact canonical spatial queries planned in `T-002`.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Procedural daylight sky contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
