#include "player_avatar_frame.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/core/math.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::terrain::HeightTileSurface make_surface()
{
    constexpr std::uint32_t sample_count = 33U;
    auto result =
        shark::terrain::HeightTileSurface::create({
            .sample_columns = sample_count,
            .sample_rows = sample_count,
            .sample_spacing = 1.0F,
            .origin = {-16.0F, 0.0F, -16.0F},
            .height_offsets = std::vector<float>(
                static_cast<std::size_t>(sample_count) *
                    sample_count,
                0.0F),
        });
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::character::PlayerCapsuleConfig
make_config()
{
    return {
        .shape = {
            .radius = 0.5F,
            .vertical_half_segment = 0.5F,
        },
        .center_bounds = {
            .minimum = {-16.0F, -8.0F, -16.0F},
            .maximum = {16.0F, 24.0F, 16.0F},
        },
        .spawn_center_position = {0.0F, 1.0F, 0.0F},
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
make_player(
    const shark::terrain::HeightTileSurface& surface,
    const shark::character::PlayerCapsuleConfig& config =
        make_config())
{
    auto result =
        shark::character::create_player_capsule(
            config,
            surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::character::PlayerCapsuleSnapshot
ground_snapshot(
    const std::uint64_t fixed_tick,
    const float horizontal_speed = 0.0F,
    const shark::character::PlayerGroundPhase phase =
        shark::character::PlayerGroundPhase::grounded)
{
    return {
        .state = {
            .center_position = {0.0F, 1.0F, 0.0F},
        },
        .vertical = {
            .phase = phase,
            .support_normal = {0.0F, 1.0F, 0.0F},
        },
        .horizontal_velocity = {
            horizontal_speed,
            0.0F,
            0.0F,
        },
        .fixed_tick = fixed_tick,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSnapshot
air_snapshot(
    const std::uint64_t fixed_tick,
    const shark::character::PlayerGroundPhase phase,
    const float velocity_y,
    const float horizontal_speed = 0.0F)
{
    return {
        .state = {
            .center_position = {0.0F, 4.0F, 0.0F},
        },
        .vertical = {
            .velocity_y = velocity_y,
            .phase = phase,
        },
        .horizontal_velocity = {
            horizontal_speed,
            0.0F,
            0.0F,
        },
        .fixed_tick = fixed_tick,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSnapshot
wading_snapshot(
    const std::uint64_t fixed_tick,
    const float horizontal_speed)
{
    auto snapshot = ground_snapshot(
        fixed_tick,
        horizontal_speed);
    snapshot.water = {
        .phase = shark::character::PlayerWaterPhase::wading,
        .depth = 0.5F,
    };
    return snapshot;
}

[[nodiscard]] shark::character::PlayerCapsuleSnapshot
swimming_snapshot(
    const std::uint64_t fixed_tick,
    const float horizontal_speed)
{
    return {
        .state = {
            .center_position = {0.0F, 1.0F, 0.0F},
        },
        .vertical = {
            .phase =
                shark::character::PlayerGroundPhase::
                    surface_swimming,
        },
        .water = {
            .phase =
                shark::character::PlayerWaterPhase::
                    surface_swimming,
            .depth = 2.0F,
            .surface_height = 1.5F,
        },
        .horizontal_velocity = {
            horizontal_speed,
            0.0F,
            0.0F,
        },
        .fixed_tick = fixed_tick,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
simulation_with_endpoints(
    const shark::terrain::HeightTileSurface& surface,
    shark::character::PlayerCapsuleSnapshot previous,
    shark::character::PlayerCapsuleSnapshot current,
    const shark::character::PlayerCapsuleConfig& config =
        make_config())
{
    auto player = make_player(surface, config);
    player.previous = std::move(previous);
    player.current = std::move(current);
    REQUIRE(shark::character::is_valid(player));
    return player;
}

[[nodiscard]] shark::sandbox::PlayerAvatarFrame frame_at_current(
    const shark::terrain::HeightTileSurface& surface,
    const shark::character::PlayerCapsuleSnapshot& current,
    const shark::character::PlayerCapsuleConfig& config =
        make_config())
{
    REQUIRE(current.fixed_tick > 0U);
    auto previous = current;
    previous.fixed_tick = current.fixed_tick - 1U;
    const auto player = simulation_with_endpoints(
        surface,
        previous,
        current,
        config);
    const auto result =
        shark::sandbox::build_player_avatar_frame(
            player,
            1.0F,
            shark::sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    REQUIRE(result);
    return result.value();
}

void require_pose(
    const shark::renderer::PlaceholderAvatarPose& actual,
    const shark::renderer::PlaceholderAvatarPose& expected,
    const float margin = 0.000001F)
{
    REQUIRE(actual.body_pitch_radians ==
        Catch::Approx(expected.body_pitch_radians)
            .margin(margin));
    REQUIRE(actual.body_vertical_offset ==
        Catch::Approx(expected.body_vertical_offset)
            .margin(margin));
    REQUIRE(actual.torso_pitch_radians ==
        Catch::Approx(expected.torso_pitch_radians)
            .margin(margin));
    REQUIRE(actual.left_arm_pitch_radians ==
        Catch::Approx(expected.left_arm_pitch_radians)
            .margin(margin));
    REQUIRE(actual.right_arm_pitch_radians ==
        Catch::Approx(expected.right_arm_pitch_radians)
            .margin(margin));
    REQUIRE(actual.left_leg_pitch_radians ==
        Catch::Approx(expected.left_leg_pitch_radians)
            .margin(margin));
    REQUIRE(actual.right_leg_pitch_radians ==
        Catch::Approx(expected.right_leg_pitch_radians)
            .margin(margin));
}

[[nodiscard]] bool pose_is_bounded(
    const shark::renderer::PlaceholderAvatarPose& pose)
{
    return std::isfinite(pose.body_pitch_radians) &&
        pose.body_pitch_radians >= -1.25F &&
        pose.body_pitch_radians <= 0.25F &&
        std::isfinite(pose.body_vertical_offset) &&
        pose.body_vertical_offset >= 0.0F &&
        pose.body_vertical_offset <= 0.40F &&
        std::isfinite(pose.torso_pitch_radians) &&
        pose.torso_pitch_radians >= -0.20F &&
        pose.torso_pitch_radians <= 0.20F &&
        std::isfinite(pose.left_arm_pitch_radians) &&
        pose.left_arm_pitch_radians >= -1.0F &&
        pose.left_arm_pitch_radians <= 1.0F &&
        std::isfinite(pose.right_arm_pitch_radians) &&
        pose.right_arm_pitch_radians >= -1.0F &&
        pose.right_arm_pitch_radians <= 1.0F &&
        std::isfinite(pose.left_leg_pitch_radians) &&
        pose.left_leg_pitch_radians >= -0.90F &&
        pose.left_leg_pitch_radians <= 0.90F &&
        std::isfinite(pose.right_leg_pitch_radians) &&
        pose.right_leg_pitch_radians >= -0.90F &&
        pose.right_leg_pitch_radians <= 0.90F;
}

[[nodiscard]] shark::water::GameplayWaterQuery dry_query(
    const shark::terrain::HeightTileSurface& surface,
    const shark::character::PlayerCapsuleSimulation& player)
{
    const auto& position =
        player.current.state.center_position;
    const auto sample = surface.sample_lod0_surface(
        position.x,
        position.z);
    REQUIRE(sample);
    return {
        .disposition =
            shark::water::GameplayWaterDisposition::no_water,
        .horizontal_support = false,
        .surface_height = sample->position.y,
        .bed_height = sample->position.y,
    };
}

[[nodiscard]] shark::sandbox::PlayerAvatarFrame
run_render_partition(const std::uint32_t render_rate_hz)
{
    using namespace shark;
    using namespace std::chrono_literals;

    const auto surface = make_surface();
    auto player = make_player(surface);
    auto clock_result = simulation::FixedStepClock::create({
        .initially_paused = false,
    });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();

    auto previous_timestamp = 0ns;
    sandbox::PlayerAvatarFrame frame;
    for (std::uint64_t render_frame = 1U;
         render_frame <= render_rate_hz;
         ++render_frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                render_frame * 1'000'000'000ULL /
                render_rate_hz)};
        const auto clock_frame =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(clock_frame);
        previous_timestamp = timestamp;

        const auto first_tick =
            clock.total_step_count() -
            clock_frame.value().step_count + 1U;
        for (std::uint32_t step = 0U;
             step < clock_frame.value().step_count;
             ++step) {
            const auto fixed_tick = first_tick + step;
            REQUIRE(character::advance_player_capsule(
                player,
                {
                    .move_forward_held = true,
                    .run_held = true,
                },
                {},
                surface,
                dry_query(surface, player),
                clock.fixed_delta_seconds(),
                fixed_tick));
        }

        const auto frame_result =
            sandbox::build_player_avatar_frame(
                player,
                clock_frame.value().interpolation_alpha,
                sandbox::PlayerAvatarInterpolationMode::
                    ordered_snapshots);
        REQUIRE(frame_result);
        frame = frame_result.value();
    }
    REQUIRE(clock.total_step_count() == 60U);
    return frame;
}

} // namespace

TEST_CASE(
    "avatar presentation classifies authoritative phases and speeds",
    "[sandbox][player-avatar][phase]")
{
    using namespace shark;

    const auto surface = make_surface();
    constexpr auto idle =
        sandbox::PlayerAvatarPresentationPhase::idle;
    constexpr auto walk =
        sandbox::PlayerAvatarPresentationPhase::walk;
    constexpr auto run =
        sandbox::PlayerAvatarPresentationPhase::run;
    constexpr auto jump =
        sandbox::PlayerAvatarPresentationPhase::jump;
    constexpr auto wade =
        sandbox::PlayerAvatarPresentationPhase::wade;
    constexpr auto swim =
        sandbox::PlayerAvatarPresentationPhase::
            surface_swimming;

    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(10U)).current_phase == idle);
    auto command_intent_only = ground_snapshot(10U);
    command_intent_only.consumed_command = {
        .move_forward_held = true,
        .run_held = true,
        .jump_pressed = true,
    };
    REQUIRE(frame_at_current(
        surface,
        command_intent_only).current_phase == idle);
    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(10U, 0.001F)).current_phase == walk);
    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(
            10U,
            std::nextafter(5.5F, 0.0F))).current_phase ==
        walk);
    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(10U, 5.5F)).current_phase == run);
    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(
            10U,
            6.0F,
            character::PlayerGroundPhase::landing))
                .current_phase == run);

    auto equal_targets = make_config();
    equal_targets.ground_locomotion.walk_speed = 4.0F;
    equal_targets.ground_locomotion.run_speed = 4.0F;
    REQUIRE(frame_at_current(
        surface,
        ground_snapshot(10U, 4.0F),
        equal_targets).current_phase == walk);

    auto steep = ground_snapshot(10U);
    steep.vertical.phase =
        character::PlayerGroundPhase::steep_contact;
    steep.vertical.support_normal = {0.8F, 0.6F, 0.0F};
    REQUIRE(frame_at_current(
        surface,
        steep).current_phase == idle);
    REQUIRE(frame_at_current(
        surface,
        air_snapshot(
            10U,
            character::PlayerGroundPhase::rising,
            2.0F,
            6.0F)).current_phase == jump);
    REQUIRE(frame_at_current(
        surface,
        air_snapshot(
            10U,
            character::PlayerGroundPhase::falling,
            -2.0F,
            6.0F)).current_phase == jump);
    REQUIRE(frame_at_current(
        surface,
        wading_snapshot(10U, 6.0F)).current_phase == wade);
    REQUIRE(frame_at_current(
        surface,
        swimming_snapshot(10U, 0.0F)).current_phase == swim);
}

TEST_CASE(
    "avatar endpoint poses use the locked deterministic formulas",
    "[sandbox][player-avatar][pose]")
{
    using namespace shark;

    const auto surface = make_surface();

    require_pose(
        frame_at_current(
            surface,
            ground_snapshot(10U)).interpolated_pose,
        {});
    require_pose(
        frame_at_current(
            surface,
            ground_snapshot(10U, 4.0F)).interpolated_pose,
        {
            .torso_pitch_radians = -0.04F,
            .left_arm_pitch_radians = -0.45F,
            .right_arm_pitch_radians = 0.45F,
            .left_leg_pitch_radians = 0.55F,
            .right_leg_pitch_radians = -0.55F,
        });
    require_pose(
        frame_at_current(
            surface,
            ground_snapshot(5U, 7.0F)).interpolated_pose,
        {
            .torso_pitch_radians = -0.16F,
            .left_arm_pitch_radians = -0.80F,
            .right_arm_pitch_radians = 0.80F,
            .left_leg_pitch_radians = 0.85F,
            .right_leg_pitch_radians = -0.85F,
        });
    require_pose(
        frame_at_current(
            surface,
            air_snapshot(
                10U,
                character::PlayerGroundPhase::rising,
                6.5F)).interpolated_pose,
        {
            .torso_pitch_radians = -0.08F,
            .left_arm_pitch_radians = 0.65F,
            .right_arm_pitch_radians = 0.65F,
            .left_leg_pitch_radians = 0.15F,
            .right_leg_pitch_radians = -0.35F,
        });
    require_pose(
        frame_at_current(
            surface,
            air_snapshot(
                10U,
                character::PlayerGroundPhase::falling,
                -13.0F)).interpolated_pose,
        {
            .torso_pitch_radians = -0.08F,
            .left_arm_pitch_radians = 0.25F,
            .right_arm_pitch_radians = 0.25F,
            .left_leg_pitch_radians = 0.35F,
            .right_leg_pitch_radians = -0.15F,
        });
    require_pose(
        frame_at_current(
            surface,
            wading_snapshot(10U, 4.0F)).interpolated_pose,
        {
            .torso_pitch_radians = -0.03F,
            .left_arm_pitch_radians = -0.37F,
            .right_arm_pitch_radians = 0.13F,
            .left_leg_pitch_radians = 0.30F,
            .right_leg_pitch_radians = -0.30F,
        });
    require_pose(
        frame_at_current(
            surface,
            swimming_snapshot(10U, 3.0F)).interpolated_pose,
        {
            .body_pitch_radians = -1.2217305F,
            .body_vertical_offset = 0.35F,
            .left_arm_pitch_radians = -0.60F,
            .right_arm_pitch_radians = 0.60F,
            .left_leg_pitch_radians = 0.25F,
            .right_leg_pitch_radians = -0.25F,
        });
    require_pose(
        frame_at_current(
            surface,
            swimming_snapshot(10U, 0.0F)).interpolated_pose,
        {
            .body_pitch_radians = -1.2217305F,
            .body_vertical_offset = 0.35F,
            .left_arm_pitch_radians = -0.15F,
            .right_arm_pitch_radians = 0.15F,
            .left_leg_pitch_radians = 0.0625F,
            .right_leg_pitch_radians = -0.0625F,
        });
}

TEST_CASE(
    "avatar presentation uses exact endpoints and one-interval pose blending",
    "[sandbox][player-avatar][interpolation]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto previous = ground_snapshot(9U, 4.0F);
    previous.state.center_position.x = -2.0F;
    previous.state.facing_yaw_radians = -0.5F;
    auto current = swimming_snapshot(10U, 3.0F);
    current.state.center_position.x = 2.0F;
    current.state.facing_yaw_radians = 0.5F;
    const auto player = simulation_with_endpoints(
        surface,
        previous,
        current);
    const auto before = player;

    const auto start =
        sandbox::build_player_avatar_frame(
            player,
            0.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto middle =
        sandbox::build_player_avatar_frame(
            player,
            0.5F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto end =
        sandbox::build_player_avatar_frame(
            player,
            1.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    REQUIRE(start);
    REQUIRE(middle);
    REQUIRE(end);
    REQUIRE(start.value().interpolated_player ==
        previous.state);
    REQUIRE(end.value().interpolated_player ==
        current.state);
    REQUIRE(middle.value().interpolated_player.center_position ==
        math::Float3{0.0F, 1.0F, 0.0F});
    REQUIRE(middle.value().interpolated_player
                .facing_yaw_radians == 0.0F);
    REQUIRE(start.value().previous_phase ==
        sandbox::PlayerAvatarPresentationPhase::walk);
    REQUIRE(start.value().current_phase ==
        sandbox::PlayerAvatarPresentationPhase::
            surface_swimming);
    REQUIRE(start.value().interpolated_pose.body_pitch_radians ==
        0.0F);
    REQUIRE(end.value().interpolated_pose.body_pitch_radians ==
        -1.2217305F);
    REQUIRE(middle.value().interpolated_pose.body_pitch_radians ==
        Catch::Approx(-1.2217305F * 0.5F));
    REQUIRE(middle.value().interpolated_pose
                .body_vertical_offset ==
        Catch::Approx(0.175F));
    REQUIRE(pose_is_bounded(start.value().interpolated_pose));
    REQUIRE(pose_is_bounded(middle.value().interpolated_pose));
    REQUIRE(pose_is_bounded(end.value().interpolated_pose));
    REQUIRE(player == before);
}

TEST_CASE(
    "avatar cycle wraps safely and remains bounded at maximum tick",
    "[sandbox][player-avatar][cycle][bounds]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto previous = ground_snapshot(39U, 4.0F);
    auto current = ground_snapshot(40U, 4.0F);
    current.state.center_position.x = 1.0F / 15.0F;
    const auto player = simulation_with_endpoints(
        surface,
        previous,
        current);
    const auto start =
        sandbox::build_player_avatar_frame(
            player,
            0.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto middle =
        sandbox::build_player_avatar_frame(
            player,
            0.5F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto end =
        sandbox::build_player_avatar_frame(
            player,
            1.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    REQUIRE(start);
    REQUIRE(middle);
    REQUIRE(end);
    const auto expected_start_wave =
        static_cast<float>(std::sin(
            static_cast<double>(math::two_pi) *
            39.0 / 40.0));
    REQUIRE(start.value().interpolated_pose
                .left_arm_pitch_radians ==
        Catch::Approx(-0.45F * expected_start_wave));
    REQUIRE(end.value().interpolated_pose
                .left_arm_pitch_radians == 0.0F);
    REQUIRE(middle.value().interpolated_pose
                .left_arm_pitch_radians ==
        Catch::Approx(
            start.value().interpolated_pose
                    .left_arm_pitch_radians *
                0.5F));

    const auto stationary_swimmer = simulation_with_endpoints(
        surface,
        swimming_snapshot(39U, 0.0F),
        swimming_snapshot(40U, 0.0F));
    const auto stationary_start =
        sandbox::build_player_avatar_frame(
            stationary_swimmer,
            0.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto stationary_middle =
        sandbox::build_player_avatar_frame(
            stationary_swimmer,
            0.5F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto stationary_end =
        sandbox::build_player_avatar_frame(
            stationary_swimmer,
            1.0F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    REQUIRE(stationary_start);
    REQUIRE(stationary_middle);
    REQUIRE(stationary_end);
    REQUIRE(stationary_start.value().interpolated_pose !=
        stationary_end.value().interpolated_pose);
    REQUIRE(stationary_middle.value().interpolated_pose
                .left_arm_pitch_radians ==
        Catch::Approx(
            (stationary_start.value().interpolated_pose
                    .left_arm_pitch_radians +
             stationary_end.value().interpolated_pose
                    .left_arm_pitch_radians) *
            0.5F));

    const auto maximum_tick =
        std::numeric_limits<std::uint64_t>::max();
    const auto maximum_player = simulation_with_endpoints(
        surface,
        swimming_snapshot(maximum_tick - 1U, 3.0F),
        swimming_snapshot(maximum_tick, 3.0F));
    const auto first =
        sandbox::build_player_avatar_frame(
            maximum_player,
            0.375F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    const auto repeated =
        sandbox::build_player_avatar_frame(
            maximum_player,
            0.375F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots);
    REQUIRE(first);
    REQUIRE(repeated);
    REQUIRE(first.value() == repeated.value());
    REQUIRE(pose_is_bounded(
        first.value().interpolated_pose));
}

TEST_CASE(
    "avatar mapping rejects invalid input and honors collapsed reset history",
    "[sandbox][player-avatar][validation][reset][collapse]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = simulation_with_endpoints(
        surface,
        ground_snapshot(1U, 4.0F),
        ground_snapshot(2U, 5.0F));
    player.current.state.center_position.x = 0.25F;
    REQUIRE(character::is_valid(player));
    REQUIRE_FALSE(
        sandbox::build_player_avatar_frame(
            player,
            0.5F,
            sandbox::PlayerAvatarInterpolationMode::
                collapsed_to_current));

    REQUIRE(character::collapse_player_capsule_interpolation(
        player));
    const auto collapsed_before = player;
    const auto collapsed_current =
        sandbox::build_player_avatar_frame(
            player,
            1.0F,
            sandbox::PlayerAvatarInterpolationMode::
                collapsed_to_current);
    REQUIRE(collapsed_current);
    for (const auto alpha :
         std::array{0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        const auto frame =
            sandbox::build_player_avatar_frame(
                player,
                alpha,
                sandbox::PlayerAvatarInterpolationMode::
                    collapsed_to_current);
        REQUIRE(frame);
        REQUIRE(frame.value().interpolated_player ==
            player.current.state);
        REQUIRE(frame.value().previous_phase ==
            sandbox::PlayerAvatarPresentationPhase::walk);
        REQUIRE(frame.value().current_phase ==
            sandbox::PlayerAvatarPresentationPhase::walk);
        require_pose(
            frame.value().interpolated_pose,
            collapsed_current.value().interpolated_pose);
        REQUIRE(pose_is_bounded(
            frame.value().interpolated_pose));
    }
    REQUIRE(player == collapsed_before);

    auto reset_player = make_player(surface);
    REQUIRE(character::advance_player_capsule(
        reset_player,
        {.reset_pressed = true},
        {},
        surface,
        dry_query(surface, reset_player),
        1.0F / 60.0F,
        1U));
    for (const auto alpha :
         std::array{0.0F, 0.5F, 1.0F}) {
        const auto frame =
            sandbox::build_player_avatar_frame(
                reset_player,
                alpha,
                sandbox::PlayerAvatarInterpolationMode::
                    collapsed_to_current);
        REQUIRE(frame);
        REQUIRE(frame.value().interpolated_player ==
            reset_player.current.state);
        REQUIRE(frame.value().previous_phase ==
            sandbox::PlayerAvatarPresentationPhase::idle);
        REQUIRE(frame.value().current_phase ==
            sandbox::PlayerAvatarPresentationPhase::idle);
        REQUIRE(frame.value().interpolated_pose ==
            renderer::PlaceholderAvatarPose{});
    }

    const auto valid_before = reset_player;
    for (const auto alpha : {
             -0.001F,
             1.001F,
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        REQUIRE_FALSE(
            sandbox::build_player_avatar_frame(
                reset_player,
                alpha,
                sandbox::PlayerAvatarInterpolationMode::
                    collapsed_to_current));
        REQUIRE(reset_player == valid_before);
    }

    auto invalid = reset_player;
    invalid.current.fixed_tick += 1U;
    const auto invalid_before = invalid;
    REQUIRE_FALSE(
        sandbox::build_player_avatar_frame(
            invalid,
            0.5F,
            sandbox::PlayerAvatarInterpolationMode::
                ordered_snapshots));
    REQUIRE(invalid == invalid_before);

    REQUIRE_FALSE(
        sandbox::build_player_avatar_frame(
            reset_player,
            0.5F,
            static_cast<
                sandbox::PlayerAvatarInterpolationMode>(255U)));
    REQUIRE(reset_player == valid_before);
}

TEST_CASE(
    "avatar presentation is invariant across exact render partitions",
    "[sandbox][player-avatar][pipeline][determinism]")
{
    const auto reference = run_render_partition(30U);
    for (const auto render_rate :
         std::array{60U, 120U, 144U}) {
        CAPTURE(render_rate);
        REQUIRE(run_render_partition(render_rate) ==
            reference);
    }
    REQUIRE(reference.previous_phase ==
        shark::sandbox::PlayerAvatarPresentationPhase::run);
    REQUIRE(reference.current_phase ==
        shark::sandbox::PlayerAvatarPresentationPhase::run);
    REQUIRE(pose_is_bounded(reference.interpolated_pose));
}
