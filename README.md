# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

The latest completed increment is `T-002`. A platform-independent
`HeightTileSurface` now owns validated canonical terrain samples and provides
exact LOD0 height, geometric-normal, bounds, and nearest-ray queries. The
sandbox proves the direct sample and downward ray agree by drawing a cyan
normal pin on the visible triangle surface. The upcoming increment is
`REN-001`, a renderer-boundary cleanup with no intended pixel or accounting
change; `T-003` terrain materials follow it.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Procedural daylight sky contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
