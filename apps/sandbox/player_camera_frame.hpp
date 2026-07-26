#pragma once

#include <shark/character/player_capsule.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/world/third_person_camera.hpp>

namespace shark::sandbox {

struct PlayerCameraFrame final {
    character::PlayerCapsuleState interpolated_player;
    world::ThirdPersonOrbitState interpolated_orbit;
    world::ThirdPersonCameraPlacement camera_placement;

    [[nodiscard]] friend bool operator==(
        const PlayerCameraFrame&,
        const PlayerCameraFrame&) noexcept = default;
};

// Pure presentation composition: interpolate the two authoritative snapshot
// streams at the same render alpha, then place the camera against canonical
// LOD0 terrain. None of the supplied simulation, rig, lens, or terrain state
// is mutated.
[[nodiscard]] core::Result<PlayerCameraFrame>
build_player_camera_frame(
    const character::PlayerCapsuleSimulation& player,
    const world::ThirdPersonCameraRig& camera_rig,
    float interpolation_alpha,
    const world::PerspectiveLens& lens,
    const terrain::HeightTileSurface& terrain_surface);

} // namespace shark::sandbox
