# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

The project is complete through `T-001`: a dedicated first `Terrain` graph
pass renders one deterministic `33x33` height tile before the checker cube and
translation-invariant skybox. The terrain exposes normal-encoded solid and
wireframe diagnostic views plus an always-present, depth-tested bounds overlay;
the canonical CPU height samples are ready for the exact spatial queries
planned in `T-002`.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Static cubemap skybox contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
