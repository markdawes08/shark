# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

The project is complete through `S-002`: a dedicated `Skybox` graph pass now
samples the project-owned DDS cubemap behind the checker cube with a
translation-invariant camera and read-only reversed-Z depth. Its diagnostic
RGB is temporarily treated as sky blue while `T-001` begins the first
deterministic terrain tile.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [Static cubemap skybox contract](docs/SKYBOX.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
