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

The latest completed increment is `T-003` (July 18, 2026). Terrain now blends
two deterministic, project-owned ground and rock layers from matching albedo,
normal, and roughness texture arrays. World-space tiling, slope/height weights,
normal mapping, and direct-sun dielectric GGX shading provide the first bounded
PBR material path. `F1` toggles solid/wireframe fill; `F2` cycles shaded,
material-weight, and world-normal views. The upcoming increment is `S-003`,
HDR environment lighting.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Procedural daylight sky contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
