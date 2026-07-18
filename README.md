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

The latest completed increment is `S-003` (July 18, 2026). Shark now generates
a deterministic linear-HDR daylight environment, derives radiance, diffuse
irradiance, GGX-prefiltered specular, and split-sum BRDF data, and applies the
same image-based lighting to terrain and a material-sphere proof. The scene
renders to an HDR target before the final `ToneMap` pass. `F1` toggles
solid/wireframe fill, `F2` cycles terrain material views, and `F3` compares HDR
IBL with the retained procedural-daylight fallback. The upcoming increment is
`T-004`, terrain chunk culling, followed by `T-005`, bounded visual LOD.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Deterministic terrain-tile contract](docs/TERRAIN.md)
- [Sky and HDR environment-lighting contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
