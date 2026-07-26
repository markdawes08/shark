#include "player_camera_frame.hpp"

#include <utility>

namespace shark::sandbox {

core::Result<PlayerCameraFrame> build_player_camera_frame(
    const character::PlayerCapsuleSimulation& player,
    const world::ThirdPersonCameraRig& camera_rig,
    const float interpolation_alpha,
    const world::PerspectiveLens& lens,
    const terrain::HeightTileSurface& terrain_surface)
{
    auto player_result = character::interpolate_player_capsule(
        player,
        interpolation_alpha);
    if (!player_result) {
        return core::Result<PlayerCameraFrame>::failure(
            std::move(player_result).error());
    }

    auto orbit_result = world::interpolate_third_person_camera_rig(
        camera_rig,
        interpolation_alpha);
    if (!orbit_result) {
        return core::Result<PlayerCameraFrame>::failure(
            std::move(orbit_result).error());
    }

    auto placement_result = world::build_third_person_camera(
        camera_rig.config,
        orbit_result.value(),
        player_result.value().center_position,
        lens,
        terrain_surface);
    if (!placement_result) {
        return core::Result<PlayerCameraFrame>::failure(
            std::move(placement_result).error());
    }

    return core::Result<PlayerCameraFrame>::success({
        .interpolated_player = player_result.value(),
        .interpolated_orbit = orbit_result.value(),
        .camera_placement =
            std::move(placement_result).value(),
    });
}

} // namespace shark::sandbox
