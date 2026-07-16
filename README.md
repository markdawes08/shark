# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

The project is complete through `S-001`: the first project-owned DDS cubemap is
strictly decoded through a DirectXTex-isolated asset boundary and uploaded as a
persistent Direct3D 12 texture cube. It is not drawn yet; `S-002` adds the
visible skybox pass.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows build and test guide](docs/BUILDING.md)
- [DDS cubemap asset and upload contract](docs/DDS_CUBEMAP.md)
