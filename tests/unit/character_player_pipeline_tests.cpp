#include "player_command_source.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/platform/events.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>
#include <shark/world/third_person_camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

inline constexpr std::size_t transcript_tick_count = 180;

struct TranscriptEntry final {
    shark::character::PlayerActionCommand command;
    shark::character::PlayerMovementFrame movement_frame;
    shark::character::PlayerCapsuleSnapshot previous;
    shark::character::PlayerCapsuleSnapshot current;
    shark::world::ThirdPersonOrbitSnapshot camera_previous;
    shark::world::ThirdPersonOrbitSnapshot camera_current;

    [[nodiscard]] friend bool operator==(
        const TranscriptEntry&,
        const TranscriptEntry&) = default;
};

struct PipelineRun final {
    std::array<TranscriptEntry, transcript_tick_count> transcript{};
    std::uint64_t emitted_ticks{};
    std::uint64_t zero_step_render_frames{};
    std::uint64_t multi_step_render_frames{};
    float final_interpolation_alpha{};
};

[[nodiscard]] shark::platform::KeyEvent key_event(
    const std::uint32_t virtual_key,
    const shark::platform::KeyAction action,
    const bool repeated = false) noexcept
{
    return {
        .virtual_key = virtual_key,
        .repeat_count = 1,
        .scan_code = 0,
        .action = action,
        .extended = false,
        .repeated = repeated,
        .system_key = false,
    };
}

void send_key(
    shark::sandbox::PlayerCommandSource& source,
    const std::uint32_t virtual_key,
    const shark::platform::KeyAction action,
    const bool repeated = false) noexcept
{
    source.handle_event(key_event(virtual_key, action, repeated));
}

void send_mouse_button(
    shark::sandbox::PlayerCommandSource& source,
    const shark::platform::MouseButton button,
    const shark::platform::ButtonAction action,
    const std::int32_t x,
    const std::int32_t y) noexcept
{
    source.handle_event(shark::platform::MouseButtonEvent{
        .x = x,
        .y = y,
        .button = button,
        .action = action,
    });
}

void apply_tick_input_transitions(
    shark::sandbox::PlayerCommandSource& source,
    const std::uint64_t fixed_tick)
{
    using namespace shark;

    switch (fixed_tick) {
    case 1:
        send_key(source, 'W', platform::KeyAction::pressed);
        send_key(source, VK_SHIFT, platform::KeyAction::pressed);
        break;
    case 12:
        send_key(source, 'D', platform::KeyAction::pressed);
        break;
    case 20:
        send_key(source, VK_SPACE, platform::KeyAction::pressed);
        send_key(
            source,
            VK_SPACE,
            platform::KeyAction::pressed,
            true);
        break;
    case 21:
        send_key(source, VK_SPACE, platform::KeyAction::released);
        break;
    case 30:
        send_key(source, VK_SPACE, platform::KeyAction::pressed);
        break;
    case 31:
        send_key(source, VK_SPACE, platform::KeyAction::released);
        send_key(source, 'W', platform::KeyAction::released);
        break;
    case 40:
        send_mouse_button(
            source,
            platform::MouseButton::left,
            platform::ButtonAction::pressed,
            0,
            0);
        break;
    case 41:
        send_mouse_button(
            source,
            platform::MouseButton::left,
            platform::ButtonAction::released,
            0,
            0);
        break;
    case 46:
        send_key(source, 'D', platform::KeyAction::released);
        send_key(source, VK_SHIFT, platform::KeyAction::released);
        break;
    case 60:
        send_key(source, 'W', platform::KeyAction::pressed);
        send_mouse_button(
            source,
            platform::MouseButton::right,
            platform::ButtonAction::pressed,
            100,
            100);
        source.handle_event(platform::MouseMovedEvent{112, 94});
        break;
    case 61:
        source.handle_event(platform::MouseMovedEvent{110, 100});
        send_mouse_button(
            source,
            platform::MouseButton::right,
            platform::ButtonAction::released,
            110,
            100);
        break;
    case 70:
        send_key(source, 'W', platform::KeyAction::released);
        break;
    case 80:
        send_key(source, 'S', platform::KeyAction::pressed);
        send_key(source, 'A', platform::KeyAction::pressed);
        break;
    case 105:
        send_key(source, 'S', platform::KeyAction::released);
        send_key(source, 'A', platform::KeyAction::released);
        break;
    case 110:
        send_key(source, 'R', platform::KeyAction::pressed);
        send_key(
            source,
            'R',
            platform::KeyAction::pressed,
            true);
        break;
    case 111:
        send_key(source, 'R', platform::KeyAction::released);
        break;
    case 120:
        send_key(source, VK_SPACE, platform::KeyAction::pressed);
        break;
    case 121:
        send_key(source, VK_SPACE, platform::KeyAction::released);
        break;
    case 130:
        send_key(source, 'W', platform::KeyAction::pressed);
        send_mouse_button(
            source,
            platform::MouseButton::right,
            platform::ButtonAction::pressed,
            200,
            200);
        source.handle_event(platform::MouseMovedEvent{212, 194});
        break;
    case 131:
        source.handle_event(platform::MouseMovedEvent{210, 200});
        send_mouse_button(
            source,
            platform::MouseButton::right,
            platform::ButtonAction::released,
            210,
            200);
        break;
    case 140:
        send_key(source, 'W', platform::KeyAction::released);
        break;
    case 170:
        send_key(source, 'R', platform::KeyAction::pressed);
        break;
    case 171:
        send_key(source, 'R', platform::KeyAction::released);
        break;
    default:
        break;
    }
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
make_player_simulation(
    const shark::terrain::HeightTileSurface& surface)
{
    auto result = shark::character::create_player_capsule({
        .shape = {
            .radius = 0.5F,
            .vertical_half_segment = 0.5F,
        },
        .center_bounds = {
            .minimum = {-32.0F, -8.0F, -32.0F},
            .maximum = {32.0F, 32.0F, 32.0F},
        },
        .spawn_center_position = {0.0F, 1.0F, 0.0F},
        .spawn_facing_yaw_radians = 0.25F,
        .ground_locomotion = {},
        .air_locomotion = {},
    }, surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface
make_flat_surface()
{
    constexpr std::uint32_t sample_count = 65U;
    auto result = shark::terrain::HeightTileSurface::create({
        .sample_columns = sample_count,
        .sample_rows = sample_count,
        .sample_spacing = 1.0F,
        .origin = {-32.0F, 0.0F, -32.0F},
        .height_offsets = std::vector<float>(
            static_cast<std::size_t>(sample_count) *
                sample_count,
            0.0F),
    });
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::water::CalmWaterBody
pipeline_water_body(
    const bool wading_schedule,
    const bool surface_swimming_schedule,
    const std::uint64_t fixed_tick)
{
    auto surface_height = -1.0F;
    if (surface_swimming_schedule) {
        surface_height = 1.5F;
        switch (fixed_tick) {
        case 111U:
            surface_height = 0.875F;
            break;
        case 112U:
            surface_height = 1.5F;
            break;
        case 113U:
            surface_height = 1.359375F;
            break;
        case 114U:
            surface_height = 1.25F;
            break;
        case 115U:
            surface_height = 0.2F;
            break;
        case 116U:
            surface_height =
                shark::character::default_player_wading_exit_depth;
            break;
        default:
            if (fixed_tick >= 117U) {
                surface_height = -1.0F;
            }
            break;
        }
    }
    else if (wading_schedule) {
        surface_height = 0.875F;
        switch (fixed_tick) {
        case 111U:
            surface_height = 0.2F;
            break;
        case 112U:
            surface_height =
                shark::character::default_player_wading_enter_depth;
            break;
        case 113U:
            surface_height = 0.2F;
            break;
        case 114U:
            surface_height =
                shark::character::default_player_wading_exit_depth;
            break;
        default:
            if (fixed_tick >= 115U) {
                surface_height = -1.0F;
            }
            break;
        }
    }
    return {
        .footprint = {
            .center_x = 0.0F,
            .center_z = 0.0F,
            .semi_axis_x = 64.0F,
            .semi_axis_z = 64.0F,
            .x_warp_square_offset = 0.0F,
            .x_warp_divisor = 4'096.0F,
            .z_warp_square_offset = 0.0F,
            .z_warp_divisor = 4'096.0F,
        },
        .support_side =
            shark::water::CalmWaterSupportSide::
                inside_warped_footprint,
        .surface_height = surface_height,
        .shoreline_depth_tolerance =
            shark::water::default_shoreline_depth_tolerance,
        .flow_velocity = shark::water::HorizontalFlow{},
    };
}

[[nodiscard]] PipelineRun run_pipeline_schedule(
    const std::uint32_t render_rate_hz,
    const bool wading_schedule = false,
    const bool surface_swimming_schedule = false)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    const auto surface = make_flat_surface();
    auto player = make_player_simulation(surface);
    auto camera_result =
        world::create_third_person_camera_rig({});
    REQUIRE(camera_result);
    auto camera_rig = std::move(camera_result).value();
    sandbox::PlayerCommandSource command_source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.01F,
        }};

    PipelineRun run;
    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) * 3U;
    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;

        const auto step_count = frame_result.value().step_count;
        run.zero_step_render_frames +=
            static_cast<std::uint64_t>(step_count == 0U);
        run.multi_step_render_frames +=
            static_cast<std::uint64_t>(step_count > 1U);
        for (std::uint32_t step = 0U;
             step < step_count;
             ++step) {
            ++run.emitted_ticks;
            REQUIRE(run.emitted_ticks <=
                run.transcript.size());
            apply_tick_input_transitions(
                command_source,
                run.emitted_ticks);
            const auto command =
                command_source.sample_fixed_tick();
            REQUIRE(character::is_valid(command));
            REQUIRE(world::advance_third_person_camera_rig(
                camera_rig,
                world::ThirdPersonOrbitDelta{
                    .yaw_radians =
                        command.look_yaw_delta_radians,
                    .pitch_radians =
                        command.look_pitch_delta_radians,
                },
                run.emitted_ticks));
            const auto camera_basis =
                world::horizontal_camera_basis(
                    camera_rig.current.state.yaw_radians);
            REQUIRE(camera_basis);
            const character::PlayerMovementFrame movement_frame{
                .right = camera_basis.value().right,
                .forward = camera_basis.value().forward,
            };
            const auto gameplay_water =
                water::query_gameplay_water(
                    pipeline_water_body(
                        wading_schedule,
                        surface_swimming_schedule,
                        run.emitted_ticks),
                    surface,
                    player.current.state.center_position.x,
                    player.current.state.center_position.z);
            REQUIRE(gameplay_water);
            REQUIRE(character::advance_player_capsule(
                player,
                command,
                movement_frame,
                surface,
                gameplay_water.value(),
                clock.fixed_delta_seconds(),
                run.emitted_ticks));
            REQUIRE(player.current.consumed_command ==
                command);
            run.transcript[
                static_cast<std::size_t>(
                    run.emitted_ticks - 1U)] = {
                        .command = command,
                        .movement_frame = movement_frame,
                        .previous = player.previous,
                        .current = player.current,
                        .camera_previous =
                            camera_rig.previous,
                        .camera_current =
                            camera_rig.current,
                    };
        }
    }

    REQUIRE(previous_timestamp == std::chrono::seconds{3});
    REQUIRE(run.emitted_ticks == transcript_tick_count);
    REQUIRE(clock.total_step_count() == transcript_tick_count);
    run.final_interpolation_alpha =
        clock.interpolation_alpha();
    return run;
}

} // namespace

TEST_CASE(
    "player command and snapshot pipeline is invariant across render rates",
    "[character][player-capsule][pipeline][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };

    const auto baseline =
        run_pipeline_schedule(render_rates.front());
    REQUIRE(baseline.multi_step_render_frames > 0U);
    REQUIRE(baseline.final_interpolation_alpha ==
        Catch::Approx(0.0F).margin(0.000001F));

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run = run_pipeline_schedule(render_rate);
        REQUIRE(run.emitted_ticks == transcript_tick_count);
        REQUIRE(run.final_interpolation_alpha ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(run.transcript == baseline.transcript);
        if (render_rate > 60U) {
            REQUIRE(run.zero_step_render_frames > 0U);
        }
    }

    const auto& tick_one = baseline.transcript[0].command;
    REQUIRE(tick_one.move_forward_held);
    REQUIRE(tick_one.run_held);
    REQUIRE_FALSE(tick_one.move_right_held);

    const auto& tick_twelve = baseline.transcript[11].command;
    REQUIRE(tick_twelve.move_forward_held);
    REQUIRE(tick_twelve.move_right_held);
    REQUIRE(tick_twelve.run_held);

    REQUIRE(baseline.transcript[19].command.jump_pressed);
    REQUIRE_FALSE(
        baseline.transcript[20].command.jump_pressed);
    REQUIRE(baseline.transcript[29].command.jump_pressed);
    REQUIRE_FALSE(
        baseline.transcript[30].command.jump_pressed);
    REQUIRE(baseline.transcript[119].command.jump_pressed);
    REQUIRE_FALSE(
        baseline.transcript[120].command.jump_pressed);
    REQUIRE(
        baseline.transcript[39]
            .command.primary_action_pressed);
    REQUIRE_FALSE(
        baseline.transcript[40]
            .command.primary_action_pressed);

    REQUIRE(
        baseline.transcript[59]
            .command.look_yaw_delta_radians ==
        Catch::Approx(0.12F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[59]
            .command.look_pitch_delta_radians ==
        Catch::Approx(0.06F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[60]
            .command.look_yaw_delta_radians ==
        Catch::Approx(-0.02F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[60]
            .command.look_pitch_delta_radians ==
        Catch::Approx(-0.06F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[61]
            .command.look_yaw_delta_radians == 0.0F);
    REQUIRE(
        baseline.transcript[61]
            .command.look_pitch_delta_radians == 0.0F);
    REQUIRE(
        baseline.transcript[129]
            .command.look_yaw_delta_radians ==
        Catch::Approx(0.12F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[130]
            .command.look_yaw_delta_radians ==
        Catch::Approx(-0.02F).margin(0.000001F));

    std::uint64_t landing_tick = 0U;
    std::uint64_t landing_count = 0U;
    std::uint64_t apex_transition_count = 0U;
    for (const auto& entry : baseline.transcript) {
        REQUIRE(entry.current.water ==
            shark::character::PlayerWaterState{});
        REQUIRE(entry.current.horizontal_velocity.y == 0.0F);
        REQUIRE(entry.current.consumed_command == entry.command);
        REQUIRE(entry.current.consumed_movement_frame ==
            entry.movement_frame);
        switch (entry.current.vertical.phase) {
        case shark::character::PlayerGroundPhase::grounded:
        case shark::character::PlayerGroundPhase::landing:
            REQUIRE(entry.current.vertical.velocity_y == 0.0F);
            REQUIRE(entry.current.vertical.support_normal ==
                shark::math::Float3{0.0F, 1.0F, 0.0F});
            REQUIRE(entry.current.state.center_position.y == 1.0F);
            break;
        case shark::character::PlayerGroundPhase::rising:
            REQUIRE(entry.current.vertical.velocity_y > 0.0F);
            REQUIRE(entry.current.vertical.support_normal ==
                shark::math::Float3{});
            REQUIRE(entry.current.state.center_position.y > 1.0F);
            break;
        case shark::character::PlayerGroundPhase::falling:
            REQUIRE(entry.current.vertical.velocity_y <= 0.0F);
            REQUIRE(entry.current.vertical.support_normal ==
                shark::math::Float3{});
            REQUIRE(entry.current.state.center_position.y > 1.0F);
            break;
        case shark::character::PlayerGroundPhase::
            surface_swimming:
        case shark::character::PlayerGroundPhase::steep_contact:
        default:
            FAIL("flat pipeline produced an invalid support phase");
        }

        if (entry.current.vertical.phase ==
            shark::character::PlayerGroundPhase::landing) {
            ++landing_count;
            landing_tick = entry.current.fixed_tick;
            REQUIRE(entry.previous.vertical.phase ==
                shark::character::PlayerGroundPhase::falling);
        }
        if (entry.previous.vertical.phase ==
                shark::character::PlayerGroundPhase::rising &&
            entry.current.vertical.phase ==
                shark::character::PlayerGroundPhase::falling) {
            ++apex_transition_count;
            REQUIRE(entry.previous.vertical.velocity_y > 0.0F);
            REQUIRE(entry.current.vertical.velocity_y <= 0.0F);
        }
    }
    REQUIRE(landing_count == 1U);
    REQUIRE(landing_tick > 80U);
    REQUIRE(landing_tick < 110U);
    REQUIRE(apex_transition_count == 2U);
    REQUIRE(baseline.transcript[
        static_cast<std::size_t>(landing_tick)]
            .current.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);

    const auto& launch_tick = baseline.transcript[19];
    REQUIRE(launch_tick.previous.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);
    REQUIRE(launch_tick.current.vertical.phase ==
        shark::character::PlayerGroundPhase::rising);
    REQUIRE(
        launch_tick.current.vertical.velocity_y ==
        Catch::Approx(
            shark::character::default_player_jump_launch_speed -
            shark::character::default_player_gravity_magnitude /
                60.0F)
            .margin(0.000001F));
    REQUIRE(
        launch_tick.current.state.center_position.y ==
        Catch::Approx(
            1.0F +
            launch_tick.current.vertical.velocity_y / 60.0F)
            .margin(0.000001F));

    const auto& ignored_double_jump = baseline.transcript[29];
    REQUIRE(ignored_double_jump.command.jump_pressed);
    REQUIRE(ignored_double_jump.previous.vertical.phase ==
        shark::character::PlayerGroundPhase::rising);
    REQUIRE(ignored_double_jump.current.vertical.phase ==
        shark::character::PlayerGroundPhase::rising);
    REQUIRE(
        ignored_double_jump.current.vertical.velocity_y ==
        Catch::Approx(
            ignored_double_jump.previous.vertical.velocity_y -
            shark::character::default_player_gravity_magnitude /
                60.0F)
            .margin(0.000001F));
    REQUIRE(
        ignored_double_jump.current.vertical.velocity_y <
        launch_tick.current.vertical.velocity_y);

    const auto& same_tick_orbit = baseline.transcript[129];
    REQUIRE(same_tick_orbit.command.move_forward_held);
    REQUIRE(same_tick_orbit.camera_previous.state.yaw_radians ==
        Catch::Approx(0.1F).margin(0.000001F));
    REQUIRE(same_tick_orbit.camera_current.state.yaw_radians ==
        Catch::Approx(0.22F).margin(0.000001F));
    REQUIRE(same_tick_orbit.previous.vertical.phase ==
        shark::character::PlayerGroundPhase::rising);
    REQUIRE(same_tick_orbit.previous.horizontal_velocity ==
        shark::math::Float3{});
    const auto same_tick_basis =
        shark::world::horizontal_camera_basis(
            same_tick_orbit.camera_current.state.yaw_radians);
    REQUIRE(same_tick_basis);
    REQUIRE(same_tick_orbit.movement_frame ==
        shark::character::PlayerMovementFrame{
            .right = same_tick_basis.value().right,
            .forward = same_tick_basis.value().forward,
        });
    const auto same_tick_delta = shark::math::Float3{
        same_tick_orbit.current.state.center_position.x -
            same_tick_orbit.previous.state.center_position.x,
        same_tick_orbit.current.state.center_position.y -
            same_tick_orbit.previous.state.center_position.y,
        same_tick_orbit.current.state.center_position.z -
            same_tick_orbit.previous.state.center_position.z,
    };
    REQUIRE(same_tick_delta.x > 0.0F);
    REQUIRE(same_tick_delta.z < 0.0F);
    const auto expected_air_speed =
        shark::character::default_player_air_control_acceleration /
        60.0F;
    REQUIRE(
        same_tick_orbit.current.horizontal_velocity.x ==
        Catch::Approx(
            same_tick_basis.value().forward.x *
                expected_air_speed)
            .margin(0.000001F));
    REQUIRE(
        same_tick_orbit.current.horizontal_velocity.z ==
        Catch::Approx(
            same_tick_basis.value().forward.z *
                expected_air_speed)
            .margin(0.000001F));
    REQUIRE(same_tick_delta.x ==
        Catch::Approx(
            same_tick_orbit.current.horizontal_velocity.x /
            60.0F)
            .margin(0.000001F));
    REQUIRE(same_tick_delta.z ==
        Catch::Approx(
            same_tick_orbit.current.horizontal_velocity.z /
            60.0F)
            .margin(0.000001F));

    REQUIRE(
        baseline.transcript[79]
            .command.move_backward_held);
    REQUIRE(
        baseline.transcript[79]
            .command.move_left_held);
    REQUIRE_FALSE(
        baseline.transcript[104]
            .command.move_backward_held);
    REQUIRE_FALSE(
        baseline.transcript[104]
            .command.move_left_held);
    const auto require_reset =
        [](const TranscriptEntry& entry,
           const std::uint64_t fixed_tick,
           const std::uint64_t generation) {
            REQUIRE(entry.command.reset_pressed);
            REQUIRE(entry.previous.fixed_tick == fixed_tick - 1U);
            REQUIRE(entry.current.fixed_tick == fixed_tick);
            REQUIRE(entry.previous.reset_generation == generation);
            REQUIRE(entry.current.reset_generation == generation);
            REQUIRE(entry.previous.state == entry.current.state);
            REQUIRE(entry.previous.vertical ==
                entry.current.vertical);
            REQUIRE(entry.previous.horizontal_velocity ==
                shark::math::Float3{});
            REQUIRE(entry.current.horizontal_velocity ==
                shark::math::Float3{});
            REQUIRE(entry.current.vertical.phase ==
                shark::character::PlayerGroundPhase::grounded);
            REQUIRE(entry.current.vertical.velocity_y == 0.0F);
            REQUIRE(entry.current.state.center_position ==
                shark::math::Float3{0.0F, 1.0F, 0.0F});
        };

    REQUIRE(baseline.transcript[108].current.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);
    require_reset(baseline.transcript[109], 110U, 1U);
    REQUIRE_FALSE(
        baseline.transcript[110].command.reset_pressed);

    REQUIRE(baseline.transcript[168].current.vertical.phase ==
        shark::character::PlayerGroundPhase::falling);
    REQUIRE(baseline.transcript[168].current.state.center_position.y >
        1.0F);
    require_reset(baseline.transcript[169], 170U, 2U);
    REQUIRE_FALSE(
        baseline.transcript[170].command.reset_pressed);
    REQUIRE(baseline.transcript.back().current.horizontal_velocity ==
        shark::math::Float3{});
    REQUIRE(baseline.transcript.back().current.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);
}

TEST_CASE(
    "wading command pipeline is exact across render partitions",
    "[character][player-capsule][pipeline][wading][fixed-step][invariance]")
{
    using namespace shark;

    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline =
        run_pipeline_schedule(render_rates.front(), true);
    REQUIRE(baseline.multi_step_render_frames > 0U);
    REQUIRE(baseline.transcript[0].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 0.875F,
        });
    REQUIRE(baseline.transcript[19].command.jump_pressed);
    REQUIRE(baseline.transcript[19].current.vertical.phase ==
        character::PlayerGroundPhase::rising);
    REQUIRE(baseline.transcript[19].current.water ==
        character::PlayerWaterState{});

    std::uint64_t landing_tick = 0U;
    for (const auto& entry : baseline.transcript) {
        if (entry.current.vertical.phase ==
            character::PlayerGroundPhase::landing) {
            landing_tick = entry.current.fixed_tick;
            REQUIRE(entry.current.water ==
                character::PlayerWaterState{});
        }
        if (entry.current.vertical.phase ==
                character::PlayerGroundPhase::rising ||
            entry.current.vertical.phase ==
                character::PlayerGroundPhase::falling) {
            REQUIRE(entry.current.water ==
                character::PlayerWaterState{});
        }
    }
    REQUIRE(landing_tick != 0U);
    REQUIRE(landing_tick < 110U);
    REQUIRE(
        baseline.transcript[
            static_cast<std::size_t>(landing_tick)]
            .current.water.phase ==
        character::PlayerWaterPhase::wading);

    REQUIRE(baseline.transcript[109].command.reset_pressed);
    REQUIRE(baseline.transcript[109].current.water ==
        character::PlayerWaterState{});
    REQUIRE(baseline.transcript[110].current.water ==
        character::PlayerWaterState{});
    REQUIRE(baseline.transcript[111].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth =
                character::default_player_wading_enter_depth,
        });
    REQUIRE(baseline.transcript[112].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 0.2F,
        });
    REQUIRE(baseline.transcript[113].current.water ==
        character::PlayerWaterState{});
    REQUIRE(baseline.transcript[114].current.water ==
        character::PlayerWaterState{});

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run =
            run_pipeline_schedule(render_rate, true);
        REQUIRE(run.emitted_ticks == transcript_tick_count);
        REQUIRE(run.final_interpolation_alpha ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(run.transcript == baseline.transcript);
        if (render_rate > 60U) {
            REQUIRE(run.zero_step_render_frames > 0U);
        }
    }
}

TEST_CASE(
    "surface-swimming command pipeline is exact across render partitions",
    "[character][player-capsule][pipeline][surface-swimming][fixed-step][invariance]")
{
    using namespace shark;

    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline =
        run_pipeline_schedule(
            render_rates.front(),
            false,
            true);
    REQUIRE(baseline.multi_step_render_frames > 0U);

    REQUIRE(baseline.transcript[0].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 1.5F,
        });
    REQUIRE(baseline.transcript[0].current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(baseline.transcript[1].current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 1.5F,
            .surface_height = 1.5F,
        });
    REQUIRE(baseline.transcript[1].current.vertical.phase ==
        character::PlayerGroundPhase::surface_swimming);

    const auto& ignored_swim_jump = baseline.transcript[19];
    REQUIRE(ignored_swim_jump.command.jump_pressed);
    REQUIRE(ignored_swim_jump.previous.vertical.phase ==
        character::PlayerGroundPhase::surface_swimming);
    REQUIRE(ignored_swim_jump.current.vertical.phase ==
        character::PlayerGroundPhase::surface_swimming);
    REQUIRE(ignored_swim_jump.current.water.phase ==
        character::PlayerWaterPhase::surface_swimming);
    REQUIRE(ignored_swim_jump.current.vertical.velocity_y ==
        0.0F);

    REQUIRE(baseline.transcript[109].command.reset_pressed);
    REQUIRE(baseline.transcript[109].previous.state ==
        baseline.transcript[109].current.state);
    REQUIRE(baseline.transcript[109].current.state.center_position ==
        math::Float3{0.0F, 1.0F, 0.0F});
    REQUIRE(baseline.transcript[109].previous.water ==
        character::PlayerWaterState{});
    REQUIRE(baseline.transcript[109].current.water ==
        character::PlayerWaterState{});
    REQUIRE(baseline.transcript[109].current.reset_generation ==
        1U);

    REQUIRE(baseline.transcript[110].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 0.875F,
        });
    REQUIRE(baseline.transcript[111].current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 1.5F,
            .surface_height = 1.5F,
        });
    REQUIRE(baseline.transcript[112].current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 1.359375F,
            .surface_height = 1.359375F,
        });
    REQUIRE(baseline.transcript[113].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 1.25F,
        });
    REQUIRE(baseline.transcript[114].current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 0.2F,
        });
    REQUIRE(baseline.transcript[115].current.water ==
        character::PlayerWaterState{});

    for (const auto& entry : baseline.transcript) {
        if (entry.current.water.phase ==
            character::PlayerWaterPhase::surface_swimming) {
            REQUIRE(entry.current.vertical.phase ==
                character::PlayerGroundPhase::surface_swimming);
            REQUIRE(entry.current.vertical.velocity_y == 0.0F);
            REQUIRE(entry.current.vertical.support_normal ==
                math::Float3{});
            REQUIRE(entry.current.state.center_position.y ==
                Catch::Approx(
                    std::max(
                        entry.current.water.surface_height - 0.5F,
                        1.0F))
                    .margin(0.000001F));
            REQUIRE(std::hypot(
                entry.current.horizontal_velocity.x,
                entry.current.horizontal_velocity.z) <=
                3.0F + 0.000001F);
        }
    }

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run =
            run_pipeline_schedule(
                render_rate,
                false,
                true);
        REQUIRE(run.emitted_ticks == transcript_tick_count);
        REQUIRE(run.final_interpolation_alpha ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(run.transcript == baseline.transcript);
        if (render_rate > 60U) {
            REQUIRE(run.zero_step_render_frames > 0U);
        }
    }
}
