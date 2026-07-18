# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

Shark's long-term feature breadth is intentionally capped at a San
Andreas-class local open-world sandbox. That is a maximum functional envelope,
not a recreation or a 2004 technology restriction: the engine remains an
independent modern Direct3D 12 project, and the current environment, physics,
and coupled-water roadmap is unchanged.

The latest completed increment is `REN-001` (July 18, 2026). The move-only
`shark::renderer::Renderer` now owns public renderer configuration, frame
input, status, statistics, and the production `Terrain -> TexturedCube ->
Skybox` frame pipeline. Its Direct3D 12 backend and scene helpers are private;
the sandbox passes the D3D12 `Device` only at the composition root. This
boundary cleanup preserves the existing pixels and exact smoke accounting.
The upcoming increment is `T-003`, layered PBR terrain materials.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Procedural daylight sky contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
