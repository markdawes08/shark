# Shark

Shark is a Windows-first, modern Direct3D 12 graphics and physics simulation
engine. Its first vertical slice is an interactive outdoor environment with a
skybox, textured terrain, rain, terrain collision, and progressively simulated
surface water.

## Project status

The architecture, Windows toolchain contract, and reproducible C++ build
foundation are defined. The repository now builds the `SharkEngine`,
`SharkSandbox`, and `SharkTests` targets in Debug and Release. The sandbox is
intentionally code-only at this point: no window, graphics device, or runtime
engine systems exist yet.

- [Engine architecture and roadmap](docs/ENGINE_PLAN.md)
- [Windows development setup](docs/WINDOWS_SETUP.md)
- [Build and test instructions](docs/BUILDING.md)
- [Pinned dependency record](docs/DEPENDENCIES.md)
- [Read-only prerequisite checker](scripts/check-prerequisites.ps1)
- Next increment: `F-003` core diagnostics foundation
