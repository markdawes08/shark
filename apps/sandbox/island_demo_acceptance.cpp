#include "island_demo_acceptance.hpp"

#include "player_avatar_frame.hpp"
#include "player_camera_frame.hpp"

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/water/gameplay_water.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace shark::sandbox {
namespace {

inline constexpr float fixed_delta_seconds = 1.0F / 60.0F;
inline constexpr float waypoint_radius = 0.2F;
inline constexpr float final_spawn_radius = 1.25F;
inline constexpr float maximum_tick_displacement = 0.5F;
inline constexpr float penetration_tolerance = 0.0001F;
inline constexpr std::uint64_t idle_orbit_tick_count = 30U;
inline constexpr std::uint64_t maximum_journey_tick_count = 6'000U;
inline constexpr std::uint64_t completion_tick_quantum = 60U;
inline constexpr std::uint64_t fnv_offset_basis =
    0xCBF2'9CE4'8422'2325ULL;
inline constexpr std::uint64_t fnv_prime =
    0x0000'0100'0000'01B3ULL;

enum class JourneyStage : std::uint8_t {
    idle_orbit = 1,
    walk_out,
    run_home,
    jump_and_enter_water,
    return_home,
    settle_home,
};

struct TranscriptHasher final {
    std::uint64_t value{fnv_offset_basis};

    void append_byte(const std::uint8_t byte) noexcept
    {
        value ^= byte;
        value *= fnv_prime;
    }

    void append(const std::uint32_t source) noexcept
    {
        for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
            append_byte(static_cast<std::uint8_t>(source >> shift));
        }
    }

    void append(const std::uint64_t source) noexcept
    {
        for (std::uint32_t shift = 0; shift < 64U; shift += 8U) {
            append_byte(static_cast<std::uint8_t>(source >> shift));
        }
    }

    void append(const float source) noexcept
    {
        append(std::bit_cast<std::uint32_t>(source));
    }

    void append(const bool source) noexcept
    {
        append_byte(static_cast<std::uint8_t>(source));
    }

    template<typename Enum>
        requires std::is_enum_v<Enum>
    void append(const Enum source) noexcept
    {
        append_byte(static_cast<std::uint8_t>(source));
    }
};

struct JourneyState final {
    JourneyStage stage{JourneyStage::idle_orbit};
    bool jump_issued{};
    bool completed{};
    TranscriptHasher transcript;
    std::array<PlayerAvatarFrame, island_demo_avatar_phase_count>
        avatar_phase_checkpoints{};
    std::array<std::uint64_t, island_demo_avatar_phase_count>
        avatar_phase_checkpoint_ticks{};
    std::array<bool, island_demo_avatar_phase_count>
        avatar_phase_checkpoint_captured{};
    IslandDemoJourneyWitness witness{
        .minimum_terrain_clearance =
            std::numeric_limits<float>::max(),
    };
};

[[nodiscard]] core::Error acceptance_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        core::ErrorCode::invalid_state,
        std::move(message),
    };
}

[[nodiscard]] core::Result<void> fail(std::string message)
{
    return core::Result<void>::failure(
        acceptance_error(std::move(message)));
}

[[nodiscard]] float canonical_zero(const float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] float horizontal_distance(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    return static_cast<float>(std::hypot(
        static_cast<double>(second.x) - first.x,
        static_cast<double>(second.z) - first.z));
}

[[nodiscard]] float spatial_distance(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    const auto delta_x =
        static_cast<double>(second.x) - first.x;
    const auto delta_y =
        static_cast<double>(second.y) - first.y;
    const auto delta_z =
        static_cast<double>(second.z) - first.z;
    return static_cast<float>(std::sqrt(
        delta_x * delta_x +
        delta_y * delta_y +
        delta_z * delta_z));
}

[[nodiscard]] float shortest_yaw_delta(
    const float desired,
    const float current) noexcept
{
    auto delta = std::remainder(
        static_cast<double>(desired) - current,
        static_cast<double>(math::two_pi));
    if (delta >= static_cast<double>(math::pi)) {
        delta -= static_cast<double>(math::two_pi);
    }
    return canonical_zero(static_cast<float>(delta));
}

[[nodiscard]] math::Float3 walk_target(
    const world::IslandDemoScenario& scenario) noexcept
{
    const auto& start = scenario.traversal_loop[0];
    const auto& next = scenario.traversal_loop[1];
    constexpr float amount = 0.125F;
    return {
        start.x + (next.x - start.x) * amount,
        0.0F,
        start.z + (next.z - start.z) * amount,
    };
}

[[nodiscard]] bool pose_is_finite(
    const renderer::PlaceholderAvatarPose& pose) noexcept
{
    return std::isfinite(pose.body_pitch_radians) &&
        std::isfinite(pose.body_vertical_offset) &&
        std::isfinite(pose.torso_pitch_radians) &&
        std::isfinite(pose.left_arm_pitch_radians) &&
        std::isfinite(pose.right_arm_pitch_radians) &&
        std::isfinite(pose.left_leg_pitch_radians) &&
        std::isfinite(pose.right_leg_pitch_radians);
}

void hash_float3(
    TranscriptHasher& hasher,
    const math::Float3 value) noexcept
{
    hasher.append(value.x);
    hasher.append(value.y);
    hasher.append(value.z);
}

void hash_command(
    TranscriptHasher& hasher,
    const character::PlayerActionCommand& command) noexcept
{
    hasher.append(command.move_forward_held);
    hasher.append(command.move_backward_held);
    hasher.append(command.move_left_held);
    hasher.append(command.move_right_held);
    hasher.append(command.run_held);
    hasher.append(command.jump_pressed);
    hasher.append(command.primary_action_pressed);
    hasher.append(command.reset_pressed);
    hasher.append(command.look_yaw_delta_radians);
    hasher.append(command.look_pitch_delta_radians);
}

void hash_tick(
    TranscriptHasher& hasher,
    const JourneyStage stage,
    const character::PlayerCapsuleSnapshot& player,
    const world::ThirdPersonOrbitSnapshot& orbit) noexcept
{
    hasher.append(stage);
    hash_float3(hasher, player.state.center_position);
    hasher.append(player.state.facing_yaw_radians);
    hasher.append(player.vertical.velocity_y);
    hasher.append(player.vertical.phase);
    hash_float3(hasher, player.vertical.support_normal);
    hasher.append(player.water.phase);
    hasher.append(player.water.depth);
    hasher.append(player.water.surface_height);
    hash_float3(hasher, player.horizontal_velocity);
    hash_command(hasher, player.consumed_command);
    hash_float3(hasher, player.consumed_movement_frame.right);
    hash_float3(hasher, player.consumed_movement_frame.forward);
    hasher.append(player.fixed_tick);
    hasher.append(player.reset_generation);
    hasher.append(orbit.state.yaw_radians);
    hasher.append(orbit.state.pitch_radians);
    hasher.append(orbit.state.boom_distance);
    hasher.append(orbit.consumed_delta.yaw_radians);
    hasher.append(orbit.consumed_delta.pitch_radians);
    hasher.append(orbit.consumed_delta.boom_distance);
    hasher.append(orbit.fixed_tick);
}

[[nodiscard]] core::Result<PlayerAvatarFrame>
compose_player_presentation(
    const character::PlayerCapsuleSimulation& player,
    const world::ThirdPersonCameraRig& camera_rig,
    const float interpolation_alpha,
    const PlayerAvatarInterpolationMode interpolation_mode,
    const world::IslandDemoScenario& scenario,
    const terrain::HeightTileSurface& terrain_surface)
{
    auto camera_result = build_player_camera_frame(
        player,
        camera_rig,
        interpolation_alpha,
        scenario.player_camera_lens,
        terrain_surface);
    if (!camera_result) {
        return core::Result<PlayerAvatarFrame>::failure(
            std::move(camera_result).error());
    }
    auto avatar_result = build_player_avatar_frame(
        player,
        interpolation_alpha,
        interpolation_mode);
    if (!avatar_result) {
        return core::Result<PlayerAvatarFrame>::failure(
            std::move(avatar_result).error());
    }
    const auto& camera = camera_result.value();
    const auto& avatar = avatar_result.value();
    if (camera.interpolated_player != avatar.interpolated_player ||
        !math::is_finite(camera.interpolated_player.center_position) ||
        !std::isfinite(
            camera.interpolated_player.facing_yaw_radians) ||
        !math::is_finite(
            camera.camera_placement.camera.transform.position) ||
        !std::isfinite(camera.interpolated_orbit.yaw_radians) ||
        !std::isfinite(camera.interpolated_orbit.pitch_radians) ||
        !std::isfinite(camera.interpolated_orbit.boom_distance) ||
        !pose_is_finite(avatar.interpolated_pose)) {
        return core::Result<PlayerAvatarFrame>::failure(
            acceptance_error(
                "Island Demo camera/avatar presentation produced "
                "divergent or non-finite state"));
    }
    return core::Result<PlayerAvatarFrame>::success(
        std::move(avatar_result).value());
}

[[nodiscard]] std::size_t avatar_phase_index(
    const PlayerAvatarPresentationPhase phase) noexcept
{
    return static_cast<std::size_t>(phase) - 1U;
}

void observe_avatar_phase(
    IslandDemoJourneyWitness& witness,
    const PlayerAvatarPresentationPhase phase) noexcept
{
    switch (phase) {
    case PlayerAvatarPresentationPhase::idle:
        witness.observed_idle_avatar = true;
        break;
    case PlayerAvatarPresentationPhase::walk:
        witness.observed_walk_avatar = true;
        break;
    case PlayerAvatarPresentationPhase::run:
        witness.observed_run_avatar = true;
        break;
    case PlayerAvatarPresentationPhase::jump:
        witness.observed_jump_avatar = true;
        break;
    case PlayerAvatarPresentationPhase::wade:
        witness.observed_wade_avatar = true;
        break;
    case PlayerAvatarPresentationPhase::surface_swimming:
        witness.observed_swim_avatar = true;
        break;
    }
}

[[nodiscard]] core::Result<void> count_water_transition(
    IslandDemoJourneyWitness& witness,
    const character::PlayerWaterPhase previous,
    const character::PlayerWaterPhase current)
{
    using character::PlayerWaterPhase;
    if (previous == current) {
        return core::Result<void>::success();
    }
    if (previous == PlayerWaterPhase::dry &&
        current == PlayerWaterPhase::wading) {
        ++witness.dry_to_wading_count;
        return core::Result<void>::success();
    }
    if (previous == PlayerWaterPhase::wading &&
        current == PlayerWaterPhase::surface_swimming) {
        ++witness.wading_to_swimming_count;
        return core::Result<void>::success();
    }
    if (previous == PlayerWaterPhase::surface_swimming &&
        current == PlayerWaterPhase::wading) {
        ++witness.swimming_to_wading_count;
        return core::Result<void>::success();
    }
    if (previous == PlayerWaterPhase::wading &&
        current == PlayerWaterPhase::dry) {
        ++witness.wading_to_dry_count;
        return core::Result<void>::success();
    }
    return fail(
        "Island Demo journey published an unexpected water-state "
        "transition");
}

[[nodiscard]] core::Result<void> validate_completion(
    JourneyState& state,
    const character::PlayerCapsuleSimulation& player,
    const world::ThirdPersonCameraRig& camera_rig,
    const world::IslandDemoScenario& scenario)
{
    auto& witness = state.witness;
    witness.returned_to_dry_land =
        player.current.water.phase ==
            character::PlayerWaterPhase::dry &&
        player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded &&
        horizontal_distance(
            player.current.state.center_position,
            scenario.player_capsule.spawn_center_position) <=
            final_spawn_radius;

    if (witness.jump_launch_count != 1U ||
        witness.falling_transition_count != 1U ||
        witness.landing_count != 1U ||
        witness.dry_to_wading_count != 1U ||
        witness.wading_to_swimming_count != 1U ||
        witness.swimming_to_wading_count != 1U ||
        witness.wading_to_dry_count != 1U ||
        witness.maximum_water_depth < 5.0F ||
        witness.minimum_terrain_clearance < -penetration_tolerance ||
        witness.maximum_tick_displacement > maximum_tick_displacement ||
        !witness.observed_camera_orbit ||
        !witness.observed_idle_avatar ||
        !witness.observed_walk_avatar ||
        !witness.observed_run_avatar ||
        !witness.observed_jump_avatar ||
        !witness.observed_wade_avatar ||
        !witness.observed_swim_avatar ||
        !std::all_of(
            state.avatar_phase_checkpoint_captured.begin(),
            state.avatar_phase_checkpoint_captured.end(),
            [](const bool captured) { return captured; }) ||
        !witness.returned_to_dry_land) {
        return fail(
            "Island Demo journey did not satisfy its complete "
            "walk/run/jump/wade/swim/return acceptance contract");
    }

    witness.completed_fixed_tick = player.current.fixed_tick;
    witness.transcript_checksum = state.transcript.value;
    witness.final_player = player.current;
    witness.final_camera_orbit = camera_rig.current;
    state.completed = true;
    return core::Result<void>::success();
}

[[nodiscard]] core::Result<void> advance_journey_tick(
    JourneyState& state,
    character::PlayerCapsuleSimulation& player,
    world::ThirdPersonCameraRig& camera_rig,
    const world::IslandDemoScenario& scenario,
    const terrain::HeightTileSurface& terrain_surface,
    const std::uint64_t fixed_tick)
{
    if (fixed_tick == 0U || fixed_tick > maximum_journey_tick_count) {
        return fail("Island Demo journey exceeded its fixed-tick budget");
    }

    const auto spawn = scenario.player_capsule.spawn_center_position;
    const auto dry_walk_target = walk_target(scenario);
    const auto deep_target = scenario.shore_entry_samples[3];

    if (state.stage == JourneyStage::idle_orbit &&
        player.current.fixed_tick >= idle_orbit_tick_count) {
        state.stage = JourneyStage::walk_out;
    }
    if (state.stage == JourneyStage::walk_out &&
        horizontal_distance(
            player.current.state.center_position,
            dry_walk_target) <= waypoint_radius) {
        state.stage = JourneyStage::run_home;
    }
    if (state.stage == JourneyStage::run_home &&
        horizontal_distance(
            player.current.state.center_position,
            spawn) <= waypoint_radius) {
        state.stage = JourneyStage::jump_and_enter_water;
    }
    if (state.stage == JourneyStage::jump_and_enter_water &&
        horizontal_distance(
            player.current.state.center_position,
            deep_target) <= waypoint_radius) {
        state.stage = JourneyStage::return_home;
    }
    if (state.stage == JourneyStage::return_home &&
        horizontal_distance(
            player.current.state.center_position,
            spawn) <= waypoint_radius) {
        state.stage = JourneyStage::settle_home;
    }
    if (state.stage == JourneyStage::settle_home &&
        player.current.horizontal_velocity == math::Float3{} &&
        horizontal_distance(
            player.current.state.center_position,
            spawn) > final_spawn_radius) {
        state.stage = JourneyStage::return_home;
    }

    character::PlayerActionCommand command;
    world::ThirdPersonOrbitDelta orbit_delta;
    math::Float3 target = player.current.state.center_position;
    auto has_target = false;

    switch (state.stage) {
    case JourneyStage::idle_orbit:
        if (fixed_tick == 1U) {
            orbit_delta.yaw_radians = 0.5F;
            command.look_yaw_delta_radians =
                orbit_delta.yaw_radians;
            state.witness.observed_camera_orbit = true;
        }
        break;
    case JourneyStage::walk_out:
        target = dry_walk_target;
        has_target = true;
        command.move_forward_held = true;
        break;
    case JourneyStage::run_home:
        target = spawn;
        has_target = true;
        command.move_forward_held = true;
        command.run_held = true;
        break;
    case JourneyStage::jump_and_enter_water:
        target = deep_target;
        has_target = true;
        command.move_forward_held = true;
        command.run_held = true;
        if (!state.jump_issued &&
            player.current.vertical.phase ==
                character::PlayerGroundPhase::grounded &&
            player.current.water.phase ==
                character::PlayerWaterPhase::dry) {
            command.jump_pressed = true;
            state.jump_issued = true;
        }
        break;
    case JourneyStage::return_home:
        target = spawn;
        has_target = true;
        command.move_forward_held = true;
        command.run_held = true;
        break;
    case JourneyStage::settle_home:
        break;
    }

    if (has_target) {
        const auto delta_x =
            static_cast<double>(target.x) -
            player.current.state.center_position.x;
        const auto delta_z =
            static_cast<double>(target.z) -
            player.current.state.center_position.z;
        const auto desired_yaw = static_cast<float>(
            std::atan2(delta_x, -delta_z));
        orbit_delta.yaw_radians = shortest_yaw_delta(
            desired_yaw,
            camera_rig.current.state.yaw_radians);
        command.look_yaw_delta_radians =
            orbit_delta.yaw_radians;
        state.witness.observed_camera_orbit =
            state.witness.observed_camera_orbit ||
            orbit_delta.yaw_radians != 0.0F;
    }

    auto camera_step_result =
        world::advance_third_person_camera_rig(
            camera_rig,
            orbit_delta,
            fixed_tick);
    if (!camera_step_result) {
        return core::Result<void>::failure(
            std::move(camera_step_result).error());
    }
    auto movement_basis_result = world::horizontal_camera_basis(
        camera_rig.current.state.yaw_radians);
    if (!movement_basis_result) {
        return core::Result<void>::failure(
            std::move(movement_basis_result).error());
    }
    const character::PlayerMovementFrame movement_frame{
        .right = movement_basis_result.value().right,
        .forward = movement_basis_result.value().forward,
    };
    auto water_result = water::query_gameplay_water(
        scenario.water.gameplay_body,
        terrain_surface,
        player.current.state.center_position.x,
        player.current.state.center_position.z);
    if (!water_result) {
        return core::Result<void>::failure(
            std::move(water_result).error());
    }

    const auto previous = player.current;
    auto player_step_result = character::advance_player_capsule(
        player,
        command,
        movement_frame,
        terrain_surface,
        water_result.value(),
        fixed_delta_seconds,
        fixed_tick);
    if (!player_step_result) {
        return core::Result<void>::failure(
            std::move(player_step_result).error());
    }

    if (!character::is_valid(player) ||
        !world::is_valid(camera_rig) ||
        player.current.fixed_tick != fixed_tick ||
        camera_rig.current.fixed_tick != fixed_tick ||
        player.current.reset_generation != 0U ||
        player.current.consumed_command.reset_pressed ||
        player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact) {
        return fail(
            "Island Demo journey produced invalid, reset, unsynchronized, "
            "or steep-contact state");
    }

    const auto tick_displacement = spatial_distance(
        previous.state.center_position,
        player.current.state.center_position);
    state.witness.maximum_tick_displacement = std::max(
        state.witness.maximum_tick_displacement,
        tick_displacement);
    if (!std::isfinite(tick_displacement) ||
        tick_displacement > maximum_tick_displacement) {
        return fail(
            "Island Demo journey detected a non-finite or teleport-sized "
            "fixed-tick displacement");
    }

    auto support_result = character::query_player_terrain_support(
        scenario.player_capsule.shape,
        scenario.player_capsule.grounding,
        terrain_surface,
        player.current.state.center_position.x,
        player.current.state.center_position.z);
    if (!support_result || !support_result.value().has_value()) {
        return fail(
            "Island Demo journey lost canonical terrain support");
    }
    const auto& support = *support_result.value();
    const auto terrain_clearance =
        player.current.state.center_position.y -
        support.center_position_y;
    state.witness.minimum_terrain_clearance = std::min(
        state.witness.minimum_terrain_clearance,
        terrain_clearance);
    if (!std::isfinite(terrain_clearance) ||
        terrain_clearance < -penetration_tolerance) {
        return fail(
            "Island Demo journey penetrated canonical terrain");
    }
    if ((player.current.vertical.phase ==
             character::PlayerGroundPhase::grounded ||
         player.current.vertical.phase ==
             character::PlayerGroundPhase::landing) &&
        player.current.state.center_position.y !=
            support.center_position_y) {
        return fail(
            "Island Demo grounded journey state diverged from exact "
            "canonical support");
    }

    state.witness.maximum_water_depth = std::max(
        state.witness.maximum_water_depth,
        water_result.value().disposition ==
                water::GameplayWaterDisposition::water
            ? water_result.value().depth
            : 0.0F);
    auto transition_result = count_water_transition(
        state.witness,
        previous.water.phase,
        player.current.water.phase);
    if (!transition_result) {
        return transition_result;
    }

    if ((previous.vertical.phase ==
             character::PlayerGroundPhase::grounded ||
         previous.vertical.phase ==
             character::PlayerGroundPhase::landing) &&
        player.current.vertical.phase ==
            character::PlayerGroundPhase::rising) {
        ++state.witness.jump_launch_count;
    }
    if (previous.vertical.phase ==
            character::PlayerGroundPhase::rising &&
        player.current.vertical.phase ==
            character::PlayerGroundPhase::falling) {
        ++state.witness.falling_transition_count;
    }
    if (previous.vertical.phase ==
            character::PlayerGroundPhase::falling &&
        player.current.vertical.phase ==
            character::PlayerGroundPhase::landing) {
        ++state.witness.landing_count;
    }

    auto presentation_result = compose_player_presentation(
        player,
        camera_rig,
        1.0F,
        PlayerAvatarInterpolationMode::ordered_snapshots,
        scenario,
        terrain_surface);
    if (!presentation_result) {
        return core::Result<void>::failure(
            std::move(presentation_result).error());
    }
    const auto& avatar_frame = presentation_result.value();
    observe_avatar_phase(
        state.witness,
        avatar_frame.current_phase);
    const auto checkpoint_index = avatar_phase_index(
        avatar_frame.current_phase);
    if (checkpoint_index >= island_demo_avatar_phase_count) {
        return fail(
            "Island Demo journey produced an invalid avatar phase");
    }
    if (!state.avatar_phase_checkpoint_captured[checkpoint_index]) {
        state.avatar_phase_checkpoints[checkpoint_index] = avatar_frame;
        state.avatar_phase_checkpoint_ticks[checkpoint_index] =
            fixed_tick;
        state.avatar_phase_checkpoint_captured[checkpoint_index] = true;
    }
    hash_tick(
        state.transcript,
        state.stage,
        player.current,
        camera_rig.current);

    const auto at_home = horizontal_distance(
        player.current.state.center_position,
        spawn) <= final_spawn_radius;
    const auto settled =
        player.current.horizontal_velocity == math::Float3{} &&
        player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded &&
        player.current.water.phase ==
            character::PlayerWaterPhase::dry;
    if (state.stage == JourneyStage::settle_home &&
        at_home && settled &&
        fixed_tick % completion_tick_quantum == 0U) {
        return validate_completion(
            state,
            player,
            camera_rig,
            scenario);
    }
    return core::Result<void>::success();
}

} // namespace

core::Result<IslandDemoAcceptanceReport>
run_island_demo_acceptance(
    const world::IslandDemoScenario& scenario,
    const terrain::HeightTileSurface& terrain_surface,
    const std::uint32_t render_rate_hz)
{
    if (render_rate_hz < 4U || render_rate_hz > 1'000U ||
        terrain_surface.tile() != scenario.terrain) {
        return core::Result<IslandDemoAcceptanceReport>::failure(
            acceptance_error(
                "Island Demo acceptance requires its canonical terrain "
                "and a 4..1000 Hz render partition"));
    }

    auto player_result = character::create_player_capsule(
        scenario.player_capsule,
        terrain_surface);
    if (!player_result) {
        return core::Result<IslandDemoAcceptanceReport>::failure(
            std::move(player_result).error());
    }
    auto player = std::move(player_result).value();
    auto camera_result = world::create_third_person_camera_rig(
        scenario.player_camera);
    if (!camera_result) {
        return core::Result<IslandDemoAcceptanceReport>::failure(
            std::move(camera_result).error());
    }
    auto camera_rig = std::move(camera_result).value();
    auto clock_result = simulation::FixedStepClock::create({
        .initially_paused = false,
    });
    if (!clock_result) {
        return core::Result<IslandDemoAcceptanceReport>::failure(
            std::move(clock_result).error());
    }
    auto clock = std::move(clock_result).value();

    JourneyState state;
    IslandDemoAcceptanceReport report;
    std::uint64_t previous_frame_boundary = 0U;
    const auto maximum_render_frames =
        maximum_journey_tick_count * render_rate_hz / 60U +
        render_rate_hz;

    for (std::uint64_t render_frame = 1U;
         render_frame <= maximum_render_frames;
         ++render_frame) {
        const auto frame_boundary =
            render_frame * 1'000'000'000ULL / render_rate_hz;
        const auto elapsed = std::chrono::nanoseconds{
            frame_boundary - previous_frame_boundary};
        previous_frame_boundary = frame_boundary;
        auto frame_result = clock.advance(elapsed);
        if (!frame_result) {
            return core::Result<IslandDemoAcceptanceReport>::failure(
                std::move(frame_result).error());
        }
        const auto frame = frame_result.value();
        if (frame.discarded_elapsed !=
            std::chrono::nanoseconds{0}) {
            return core::Result<
                IslandDemoAcceptanceReport>::failure(
                    acceptance_error(
                        "Island Demo acceptance render partition "
                        "discarded elapsed time"));
        }
        report.render_frame_count = render_frame;
        report.zero_step_render_frame_count +=
            static_cast<std::uint64_t>(frame.step_count == 0U);
        report.multi_step_render_frame_count +=
            static_cast<std::uint64_t>(frame.step_count > 1U);

        const auto first_fixed_tick =
            clock.total_step_count() - frame.step_count + 1U;
        for (std::uint32_t step = 0U;
             step < frame.step_count;
             ++step) {
            auto step_result = advance_journey_tick(
                state,
                player,
                camera_rig,
                scenario,
                terrain_surface,
                first_fixed_tick + step);
            if (!step_result) {
                return core::Result<
                    IslandDemoAcceptanceReport>::failure(
                        std::move(step_result).error());
            }
            if (state.completed) {
                break;
            }
        }

        const auto interpolation_alpha = state.completed
            ? 1.0F
            : frame.interpolation_alpha;
        const auto interpolation_mode =
            player.current.fixed_tick == 0U
            ? PlayerAvatarInterpolationMode::collapsed_to_current
            : PlayerAvatarInterpolationMode::ordered_snapshots;
        auto presentation_result = compose_player_presentation(
            player,
            camera_rig,
            interpolation_alpha,
            interpolation_mode,
            scenario,
            terrain_surface);
        if (!presentation_result) {
            return core::Result<
                IslandDemoAcceptanceReport>::failure(
                    std::move(presentation_result).error());
        }
        ++report.camera_frame_count;
        ++report.avatar_frame_count;

        if (state.completed) {
            report.journey = state.witness;
            report.avatar_phase_checkpoints =
                state.avatar_phase_checkpoints;
            report.avatar_phase_checkpoint_ticks =
                state.avatar_phase_checkpoint_ticks;
            return core::Result<IslandDemoAcceptanceReport>::success(
                report);
        }
    }

    return core::Result<IslandDemoAcceptanceReport>::failure(
        acceptance_error(
            "Island Demo journey did not complete before its render-frame "
            "budget"));
}

} // namespace shark::sandbox
