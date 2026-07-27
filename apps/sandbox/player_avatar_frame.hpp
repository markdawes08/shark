#pragma once

#include <shark/character/player_capsule.hpp>
#include <shark/core/result.hpp>
#include <shark/renderer/renderer.hpp>

#include <cstdint>

namespace shark::sandbox {

enum class PlayerAvatarPresentationPhase : std::uint8_t {
    idle = 1,
    walk,
    run,
    jump,
    wade,
    surface_swimming,
};

enum class PlayerAvatarInterpolationMode : std::uint8_t {
    ordered_snapshots = 1,
    collapsed_to_current,
};

struct PlayerAvatarFrame final {
    character::PlayerCapsuleState interpolated_player;
    PlayerAvatarPresentationPhase previous_phase{
        PlayerAvatarPresentationPhase::idle};
    PlayerAvatarPresentationPhase current_phase{
        PlayerAvatarPresentationPhase::idle};
    renderer::PlaceholderAvatarPose interpolated_pose;

    [[nodiscard]] friend bool operator==(
        const PlayerAvatarFrame&,
        const PlayerAvatarFrame&) noexcept = default;
};

// Pure presentation composition. The authoritative endpoint snapshots select
// their own bounded pose, then those seven renderer-neutral scalars and the
// player root use the same render interpolation alpha. No animation state,
// command intent, wall-clock time, or simulation authority is introduced.
// collapsed_to_current is required only after the caller has explicitly
// collapsed the character's presentation history at a time discontinuity.
[[nodiscard]] core::Result<PlayerAvatarFrame>
build_player_avatar_frame(
    const character::PlayerCapsuleSimulation& player,
    float interpolation_alpha,
    PlayerAvatarInterpolationMode interpolation_mode);

} // namespace shark::sandbox
