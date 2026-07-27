#include "player_avatar_frame.hpp"

#include <shark/core/math.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace shark::sandbox {
namespace {

inline constexpr std::uint64_t avatar_cycle_period_ticks = 40U;

struct EndpointPresentation final {
    PlayerAvatarPresentationPhase phase{
        PlayerAvatarPresentationPhase::idle};
    renderer::PlaceholderAvatarPose pose;
};

[[nodiscard]] double horizontal_speed(
    const character::PlayerCapsuleSnapshot& snapshot) noexcept
{
    const auto x =
        static_cast<double>(snapshot.horizontal_velocity.x);
    const auto z =
        static_cast<double>(snapshot.horizontal_velocity.z);
    return std::sqrt(x * x + z * z);
}

[[nodiscard]] float normalized_speed(
    const double speed,
    const float target_speed) noexcept
{
    return static_cast<float>(std::clamp(
        speed / static_cast<double>(target_speed),
        0.0,
        1.0));
}

[[nodiscard]] float cycle_wave(
    const std::uint64_t fixed_tick,
    const double harmonic) noexcept
{
    const auto cycle_tick =
        fixed_tick % avatar_cycle_period_ticks;
    const auto phase =
        static_cast<double>(math::two_pi) *
        static_cast<double>(cycle_tick) /
        static_cast<double>(avatar_cycle_period_ticks);
    return static_cast<float>(std::sin(phase * harmonic));
}

[[nodiscard]] PlayerAvatarPresentationPhase classify_phase(
    const character::PlayerCapsuleSnapshot& snapshot,
    const character::PlayerCapsuleConfig& config) noexcept
{
    if (snapshot.water.phase ==
        character::PlayerWaterPhase::surface_swimming) {
        return PlayerAvatarPresentationPhase::surface_swimming;
    }

    if (snapshot.vertical.phase ==
            character::PlayerGroundPhase::rising ||
        snapshot.vertical.phase ==
            character::PlayerGroundPhase::falling) {
        return PlayerAvatarPresentationPhase::jump;
    }

    if (snapshot.water.phase ==
        character::PlayerWaterPhase::wading) {
        return PlayerAvatarPresentationPhase::wade;
    }

    if (snapshot.vertical.phase ==
        character::PlayerGroundPhase::steep_contact) {
        return PlayerAvatarPresentationPhase::idle;
    }

    const auto speed = horizontal_speed(snapshot);
    if (speed == 0.0) {
        return PlayerAvatarPresentationPhase::idle;
    }

    const auto walk_speed = static_cast<double>(
        config.ground_locomotion.walk_speed);
    const auto run_speed = static_cast<double>(
        config.ground_locomotion.run_speed);
    const auto run_threshold =
        walk_speed + (run_speed - walk_speed) * 0.5;
    if (run_speed > walk_speed &&
        speed >= run_threshold) {
        return PlayerAvatarPresentationPhase::run;
    }
    return PlayerAvatarPresentationPhase::walk;
}

[[nodiscard]] renderer::PlaceholderAvatarPose make_pose(
    const PlayerAvatarPresentationPhase phase,
    const character::PlayerCapsuleSnapshot& snapshot,
    const character::PlayerCapsuleConfig& config) noexcept
{
    renderer::PlaceholderAvatarPose pose;
    const auto speed = horizontal_speed(snapshot);
    const auto wave = cycle_wave(snapshot.fixed_tick, 1.0);

    switch (phase) {
    case PlayerAvatarPresentationPhase::idle:
        break;
    case PlayerAvatarPresentationPhase::walk: {
        const auto amount = normalized_speed(
            speed,
            config.ground_locomotion.walk_speed);
        pose.torso_pitch_radians = -0.04F * amount;
        pose.left_arm_pitch_radians =
            -0.45F * amount * wave;
        pose.right_arm_pitch_radians =
            0.45F * amount * wave;
        pose.left_leg_pitch_radians =
            0.55F * amount * wave;
        pose.right_leg_pitch_radians =
            -0.55F * amount * wave;
        break;
    }
    case PlayerAvatarPresentationPhase::run: {
        const auto amount = normalized_speed(
            speed,
            config.ground_locomotion.run_speed);
        const auto run_wave =
            cycle_wave(snapshot.fixed_tick, 2.0);
        pose.torso_pitch_radians = -0.16F * amount;
        pose.left_arm_pitch_radians =
            -0.80F * amount * run_wave;
        pose.right_arm_pitch_radians =
            0.80F * amount * run_wave;
        pose.left_leg_pitch_radians =
            0.85F * amount * run_wave;
        pose.right_leg_pitch_radians =
            -0.85F * amount * run_wave;
        break;
    }
    case PlayerAvatarPresentationPhase::jump: {
        const auto vertical_amount = std::clamp(
            snapshot.vertical.velocity_y /
                config.air_locomotion.jump_launch_speed,
            -1.0F,
            1.0F);
        pose.torso_pitch_radians = -0.08F;
        pose.left_arm_pitch_radians =
            0.45F + 0.20F * vertical_amount;
        pose.right_arm_pitch_radians =
            0.45F + 0.20F * vertical_amount;
        pose.left_leg_pitch_radians =
            0.25F - 0.10F * vertical_amount;
        pose.right_leg_pitch_radians =
            -0.25F - 0.10F * vertical_amount;
        break;
    }
    case PlayerAvatarPresentationPhase::wade: {
        const auto amount = normalized_speed(
            speed,
            config.ground_locomotion.walk_speed);
        pose.torso_pitch_radians = -0.03F;
        pose.left_arm_pitch_radians =
            -0.12F - 0.25F * amount * wave;
        pose.right_arm_pitch_radians =
            -0.12F + 0.25F * amount * wave;
        pose.left_leg_pitch_radians =
            0.30F * amount * wave;
        pose.right_leg_pitch_radians =
            -0.30F * amount * wave;
        break;
    }
    case PlayerAvatarPresentationPhase::surface_swimming: {
        const auto amount =
            0.25F +
            0.75F * normalized_speed(
                speed,
                config.surface_swimming.speed);
        pose.body_pitch_radians = -1.2217305F;
        pose.body_vertical_offset = 0.35F;
        pose.left_arm_pitch_radians =
            -0.60F * amount * wave;
        pose.right_arm_pitch_radians =
            0.60F * amount * wave;
        pose.left_leg_pitch_radians =
            0.25F * amount * wave;
        pose.right_leg_pitch_radians =
            -0.25F * amount * wave;
        break;
    }
    }
    return pose;
}

[[nodiscard]] EndpointPresentation evaluate_endpoint(
    const character::PlayerCapsuleSnapshot& snapshot,
    const character::PlayerCapsuleConfig& config) noexcept
{
    const auto phase = classify_phase(snapshot, config);
    return {
        .phase = phase,
        .pose = make_pose(phase, snapshot, config),
    };
}

[[nodiscard]] bool presentation_history_is_collapsed(
    const character::PlayerCapsuleSnapshot& previous,
    const character::PlayerCapsuleSnapshot& current) noexcept
{
    // This mirrors the payload copied by
    // collapse_player_capsule_interpolation(). Fixed ticks and consumed input
    // intentionally remain authoritative and ordered, so they cannot identify
    // the collapsed presentation interval.
    return previous.state == current.state &&
        previous.vertical == current.vertical &&
        previous.water == current.water &&
        previous.horizontal_velocity ==
            current.horizontal_velocity;
}

[[nodiscard]] float interpolate_scalar(
    const float previous,
    const float current,
    const float alpha) noexcept
{
    if (alpha == 0.0F) {
        return previous;
    }
    if (alpha == 1.0F) {
        return current;
    }
    return static_cast<float>(
        static_cast<double>(previous) +
        (static_cast<double>(current) -
         static_cast<double>(previous)) *
            static_cast<double>(alpha));
}

[[nodiscard]] renderer::PlaceholderAvatarPose interpolate_pose(
    const renderer::PlaceholderAvatarPose& previous,
    const renderer::PlaceholderAvatarPose& current,
    const float alpha) noexcept
{
    if (alpha == 0.0F) {
        return previous;
    }
    if (alpha == 1.0F) {
        return current;
    }
    return {
        .body_pitch_radians = interpolate_scalar(
            previous.body_pitch_radians,
            current.body_pitch_radians,
            alpha),
        .body_vertical_offset = interpolate_scalar(
            previous.body_vertical_offset,
            current.body_vertical_offset,
            alpha),
        .torso_pitch_radians = interpolate_scalar(
            previous.torso_pitch_radians,
            current.torso_pitch_radians,
            alpha),
        .left_arm_pitch_radians = interpolate_scalar(
            previous.left_arm_pitch_radians,
            current.left_arm_pitch_radians,
            alpha),
        .right_arm_pitch_radians = interpolate_scalar(
            previous.right_arm_pitch_radians,
            current.right_arm_pitch_radians,
            alpha),
        .left_leg_pitch_radians = interpolate_scalar(
            previous.left_leg_pitch_radians,
            current.left_leg_pitch_radians,
            alpha),
        .right_leg_pitch_radians = interpolate_scalar(
            previous.right_leg_pitch_radians,
            current.right_leg_pitch_radians,
            alpha),
    };
}

} // namespace

core::Result<PlayerAvatarFrame> build_player_avatar_frame(
    const character::PlayerCapsuleSimulation& player,
    const float interpolation_alpha,
    const PlayerAvatarInterpolationMode interpolation_mode)
{
    auto player_result = character::interpolate_player_capsule(
        player,
        interpolation_alpha);
    if (!player_result) {
        return core::Result<PlayerAvatarFrame>::failure(
            std::move(player_result).error());
    }
    if (interpolation_mode !=
            PlayerAvatarInterpolationMode::ordered_snapshots &&
        interpolation_mode !=
            PlayerAvatarInterpolationMode::collapsed_to_current) {
        return core::Result<PlayerAvatarFrame>::failure({
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_argument,
            "Player-avatar interpolation mode is invalid",
        });
    }
    if (interpolation_mode ==
            PlayerAvatarInterpolationMode::collapsed_to_current &&
        !presentation_history_is_collapsed(
            player.previous,
            player.current)) {
        return core::Result<PlayerAvatarFrame>::failure({
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_argument,
            "Player-avatar collapsed interpolation requires collapsed "
            "character presentation history",
        });
    }

    const auto current = evaluate_endpoint(
        player.current,
        player.config);
    auto previous = evaluate_endpoint(
        player.previous,
        player.config);
    if (interpolation_mode ==
        PlayerAvatarInterpolationMode::collapsed_to_current) {
        // A time-baseline discontinuity collapses all visible player history.
        // Use the current gait sample at both endpoints as well, otherwise the
        // still-ordered fixed ticks would reintroduce a one-tick limb smear.
        previous = current;
    }
    return core::Result<PlayerAvatarFrame>::success({
        .interpolated_player =
            std::move(player_result).value(),
        .previous_phase = previous.phase,
        .current_phase = current.phase,
        .interpolated_pose = interpolate_pose(
            previous.pose,
            current.pose,
            interpolation_alpha),
    });
}

} // namespace shark::sandbox
