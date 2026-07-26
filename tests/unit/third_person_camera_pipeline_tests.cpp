#include "camera_distance_input_source.hpp"
#include "player_command_source.hpp"

#include <shark/platform/events.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/world/third_person_camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

inline constexpr std::size_t transcript_tick_count = 120;

struct CameraPipelineTranscriptEntry final {
    shark::character::PlayerActionCommand sampled_player_command;
    float sampled_boom_delta{};
    shark::world::ThirdPersonOrbitDelta applied_orbit_delta;
    shark::world::ThirdPersonOrbitSnapshot previous;
    shark::world::ThirdPersonOrbitSnapshot current;
    bool free_fly_enabled{};

    [[nodiscard]] friend bool operator==(
        const CameraPipelineTranscriptEntry&,
        const CameraPipelineTranscriptEntry&) = default;
};

struct CameraPipelineRun final {
    std::array<
        CameraPipelineTranscriptEntry,
        transcript_tick_count>
        transcript{};
    shark::world::ThirdPersonCameraRig final_rig;
    std::uint64_t emitted_ticks{};
    std::uint64_t zero_step_render_frames{};
    std::uint64_t multi_step_render_frames{};
    float final_interpolation_alpha{};
};

void send_mouse_button(
    shark::sandbox::PlayerCommandSource& source,
    const shark::platform::ButtonAction action,
    const std::int32_t x,
    const std::int32_t y) noexcept
{
    source.handle_event(shark::platform::MouseButtonEvent{
        .x = x,
        .y = y,
        .button = shark::platform::MouseButton::right,
        .action = action,
    });
}

void send_wheel(
    shark::sandbox::CameraDistanceInputSource& source,
    const std::int32_t delta) noexcept
{
    source.handle_event(shark::platform::MouseWheelEvent{
        .x = 100,
        .y = 100,
        .delta = delta,
        .axis = shark::platform::MouseWheelAxis::vertical,
    });
}

void apply_tick_input_transitions(
    shark::sandbox::PlayerCommandSource& player_source,
    shark::sandbox::CameraDistanceInputSource& distance_source,
    bool& free_fly_enabled,
    const std::uint64_t fixed_tick)
{
    using namespace shark;

    switch (fixed_tick) {
    case 1:
        send_mouse_button(
            player_source,
            platform::ButtonAction::pressed,
            100,
            100);
        player_source.handle_event(
            platform::MouseMovedEvent{112, 94});
        break;
    case 2:
        player_source.handle_event(
            platform::MouseMovedEvent{110, 100});
        send_mouse_button(
            player_source,
            platform::ButtonAction::released,
            110,
            100);
        break;
    case 10:
        send_wheel(distance_source, 120);
        break;
    case 20:
        send_wheel(distance_source, -240);
        break;
    case 40:
        free_fly_enabled = true;
        player_source.reset();
        distance_source.reset();
        break;
    case 41:
        send_mouse_button(
            player_source,
            platform::ButtonAction::pressed,
            200,
            200);
        player_source.handle_event(
            platform::MouseMovedEvent{230, 170});
        send_wheel(distance_source, 240);
        break;
    case 43:
        player_source.handle_event(
            platform::MouseMovedEvent{220, 190});
        send_mouse_button(
            player_source,
            platform::ButtonAction::released,
            220,
            190);
        send_wheel(distance_source, -120);
        break;
    case 50:
        free_fly_enabled = false;
        player_source.reset();
        distance_source.reset();
        break;
    case 51:
        send_mouse_button(
            player_source,
            platform::ButtonAction::pressed,
            50,
            50);
        player_source.handle_event(
            platform::MouseMovedEvent{55, 45});
        send_mouse_button(
            player_source,
            platform::ButtonAction::released,
            55,
            45);
        send_wheel(distance_source, 120);
        break;
    default:
        break;
    }
}

[[nodiscard]] shark::world::ThirdPersonCameraRig make_camera_rig()
{
    shark::world::ThirdPersonCameraConfig config{
        .target_height_offset = 0.75F,
        .minimum_pitch_radians = -1.0F,
        .maximum_pitch_radians = 0.3F,
        .initial_yaw_radians = 0.0F,
        .initial_pitch_radians = -0.25F,
        .minimum_boom_distance = 2.0F,
        .maximum_boom_distance = 12.0F,
        .initial_boom_distance = 8.0F,
        .obstruction_clearance = 0.35F,
    };
    auto result =
        shark::world::create_third_person_camera_rig(config);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] CameraPipelineRun run_camera_pipeline_schedule(
    const std::uint32_t render_rate_hz)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    auto rig = make_camera_rig();
    sandbox::PlayerCommandSource player_source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.01F,
        }};
    sandbox::CameraDistanceInputSource distance_source{
        sandbox::CameraDistanceInputSourceConfig{
            .meters_per_wheel_detent = 1.5F,
            .maximum_absolute_delta_meters = 6.0F,
        }};

    CameraPipelineRun run;
    auto free_fly_enabled = false;
    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) * 2U;
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
                player_source,
                distance_source,
                free_fly_enabled,
                run.emitted_ticks);

            const auto sampled_player_command =
                player_source.sample_fixed_tick();
            const auto sampled_boom_delta =
                distance_source.sample_fixed_tick();
            const world::ThirdPersonOrbitDelta applied_delta =
                free_fly_enabled
                ? world::ThirdPersonOrbitDelta{}
                : world::ThirdPersonOrbitDelta{
                    .yaw_radians =
                        sampled_player_command
                            .look_yaw_delta_radians,
                    .pitch_radians =
                        sampled_player_command
                            .look_pitch_delta_radians,
                    .boom_distance = sampled_boom_delta,
                };
            REQUIRE(world::advance_third_person_camera_rig(
                rig,
                applied_delta,
                run.emitted_ticks));
            REQUIRE(rig.current.consumed_delta ==
                applied_delta);
            run.transcript[
                static_cast<std::size_t>(
                    run.emitted_ticks - 1U)] = {
                        .sampled_player_command =
                            sampled_player_command,
                        .sampled_boom_delta =
                            sampled_boom_delta,
                        .applied_orbit_delta =
                            applied_delta,
                        .previous = rig.previous,
                        .current = rig.current,
                        .free_fly_enabled =
                            free_fly_enabled,
                    };
        }
    }

    REQUIRE(previous_timestamp == std::chrono::seconds{2});
    REQUIRE(run.emitted_ticks == transcript_tick_count);
    REQUIRE(clock.total_step_count() == transcript_tick_count);
    REQUIRE(world::is_valid(rig));
    run.final_rig = rig;
    run.final_interpolation_alpha =
        clock.interpolation_alpha();
    return run;
}

} // namespace

TEST_CASE(
    "third-person camera input pipeline is invariant across render rates",
    "[world][third-person-camera][pipeline][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };

    const auto baseline =
        run_camera_pipeline_schedule(render_rates.front());
    REQUIRE(baseline.multi_step_render_frames > 0U);
    REQUIRE(baseline.final_interpolation_alpha ==
        Catch::Approx(0.0F).margin(0.000001F));

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run =
            run_camera_pipeline_schedule(render_rate);
        REQUIRE(run.emitted_ticks == transcript_tick_count);
        REQUIRE(run.final_interpolation_alpha ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(run.transcript == baseline.transcript);
        REQUIRE(run.final_rig == baseline.final_rig);
        if (render_rate > 60U) {
            REQUIRE(run.zero_step_render_frames > 0U);
        }
    }

    const auto& first_orbit = baseline.transcript[0];
    REQUIRE(first_orbit.applied_orbit_delta.yaw_radians ==
        Catch::Approx(0.12F).margin(0.000001F));
    REQUIRE(first_orbit.applied_orbit_delta.pitch_radians ==
        Catch::Approx(0.06F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[1]
            .applied_orbit_delta.yaw_radians ==
        Catch::Approx(-0.02F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[1]
            .applied_orbit_delta.pitch_radians ==
        Catch::Approx(-0.06F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[2]
            .applied_orbit_delta ==
        shark::world::ThirdPersonOrbitDelta{});

    REQUIRE(
        baseline.transcript[9].sampled_boom_delta ==
        Catch::Approx(-1.5F).margin(0.000001F));
    REQUIRE(
        baseline.transcript[9]
            .applied_orbit_delta.boom_distance ==
        Catch::Approx(-1.5F).margin(0.000001F));
    REQUIRE(baseline.transcript[10].sampled_boom_delta ==
        0.0F);
    REQUIRE(
        baseline.transcript[19].sampled_boom_delta ==
        Catch::Approx(3.0F).margin(0.000001F));
}

TEST_CASE(
    "free-fly mode samples and neutralizes pending orbit input",
    "[world][third-person-camera][pipeline][free-fly]")
{
    const auto run = run_camera_pipeline_schedule(144U);

    const auto& free_fly_orbit = run.transcript[40];
    REQUIRE(free_fly_orbit.free_fly_enabled);
    REQUIRE(
        free_fly_orbit
            .sampled_player_command.look_yaw_delta_radians ==
        Catch::Approx(0.3F).margin(0.000001F));
    REQUIRE(
        free_fly_orbit
            .sampled_player_command.look_pitch_delta_radians ==
        Catch::Approx(0.3F).margin(0.000001F));
    REQUIRE(free_fly_orbit.sampled_boom_delta ==
        Catch::Approx(-3.0F).margin(0.000001F));
    REQUIRE(free_fly_orbit.applied_orbit_delta ==
        shark::world::ThirdPersonOrbitDelta{});
    REQUIRE(free_fly_orbit.current.state ==
        free_fly_orbit.previous.state);

    // The free-fly tick consumed the raw inputs; they do not leak into the
    // following authoritative camera tick.
    const auto& next_free_fly_tick = run.transcript[41];
    REQUIRE(next_free_fly_tick.sampled_player_command ==
        shark::character::PlayerActionCommand{});
    REQUIRE(next_free_fly_tick.sampled_boom_delta == 0.0F);
    REQUIRE(next_free_fly_tick.applied_orbit_delta ==
        shark::world::ThirdPersonOrbitDelta{});

    const auto& later_free_fly_orbit = run.transcript[42];
    REQUIRE(later_free_fly_orbit.free_fly_enabled);
    REQUIRE(
        later_free_fly_orbit
            .sampled_player_command.look_yaw_delta_radians ==
        Catch::Approx(-0.1F).margin(0.000001F));
    REQUIRE(later_free_fly_orbit.sampled_boom_delta ==
        Catch::Approx(1.5F).margin(0.000001F));
    REQUIRE(later_free_fly_orbit.applied_orbit_delta ==
        shark::world::ThirdPersonOrbitDelta{});

    const auto& resumed_orbit = run.transcript[50];
    REQUIRE_FALSE(resumed_orbit.free_fly_enabled);
    REQUIRE(
        resumed_orbit.applied_orbit_delta.yaw_radians ==
        Catch::Approx(0.05F).margin(0.000001F));
    REQUIRE(
        resumed_orbit.applied_orbit_delta.pitch_radians ==
        Catch::Approx(0.05F).margin(0.000001F));
    REQUIRE(
        resumed_orbit.applied_orbit_delta.boom_distance ==
        Catch::Approx(-1.5F).margin(0.000001F));
}

TEST_CASE(
    "paused camera input is consumed once by single-step and resume",
    "[world][third-person-camera][pipeline][pause]")
{
    using namespace shark;
    using namespace std::chrono_literals;

    auto clock_result =
        simulation::FixedStepClock::create();
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    REQUIRE(clock.paused());

    auto rig = make_camera_rig();
    const auto initial_rig = rig;
    sandbox::PlayerCommandSource player_source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.01F,
        }};
    sandbox::CameraDistanceInputSource distance_source{
        sandbox::CameraDistanceInputSourceConfig{
            .meters_per_wheel_detent = 1.5F,
            .maximum_absolute_delta_meters = 6.0F,
        }};

    send_mouse_button(
        player_source,
        platform::ButtonAction::pressed,
        100,
        100);
    player_source.handle_event(
        platform::MouseMovedEvent{112, 94});
    send_mouse_button(
        player_source,
        platform::ButtonAction::released,
        112,
        94);
    send_wheel(distance_source, 120);

    // Main starts paused. No fixed-tick sample occurs while render frames
    // continue, so both input sources retain their pending deltas.
    const std::array paused_elapsed_values{
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(8ms),
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(250ms),
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(2s),
    };
    for (const auto paused_elapsed :
         paused_elapsed_values) {
        const auto paused_frame =
            clock.advance(paused_elapsed);
        REQUIRE(paused_frame);
        REQUIRE(paused_frame.value().step_count == 0U);
        REQUIRE(paused_frame.value().accepted_elapsed == 0ns);
        REQUIRE(paused_frame.value().discarded_elapsed ==
            paused_elapsed);
        REQUIRE(clock.total_step_count() == 0U);
        REQUIRE(rig == initial_rig);
    }

    // F6 queues exactly one authoritative tick while paused.
    REQUIRE(clock.request_single_step());
    const auto single_step_frame = clock.advance(1s);
    REQUIRE(single_step_frame);
    REQUIRE(single_step_frame.value().step_count == 1U);
    REQUIRE(single_step_frame.value().accepted_elapsed == 0ns);
    REQUIRE(single_step_frame.value().discarded_elapsed == 1s);
    REQUIRE(clock.total_step_count() == 1U);

    const auto first_command =
        player_source.sample_fixed_tick();
    const auto first_boom =
        distance_source.sample_fixed_tick();
    const world::ThirdPersonOrbitDelta first_delta{
        .yaw_radians =
            first_command.look_yaw_delta_radians,
        .pitch_radians =
            first_command.look_pitch_delta_radians,
        .boom_distance = first_boom,
    };
    REQUIRE(first_delta.yaw_radians ==
        Catch::Approx(0.12F).margin(0.000001F));
    REQUIRE(first_delta.pitch_radians ==
        Catch::Approx(0.06F).margin(0.000001F));
    REQUIRE(first_delta.boom_distance ==
        Catch::Approx(-1.5F).margin(0.000001F));
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        first_delta,
        1U));
    REQUIRE(rig.current.consumed_delta == first_delta);

    send_mouse_button(
        player_source,
        platform::ButtonAction::pressed,
        50,
        50);
    player_source.handle_event(
        platform::MouseMovedEvent{45, 55});
    send_mouse_button(
        player_source,
        platform::ButtonAction::released,
        45,
        55);
    send_wheel(distance_source, -120);
    const auto waiting_frame = clock.advance(5s);
    REQUIRE(waiting_frame);
    REQUIRE(waiting_frame.value().step_count == 0U);
    REQUIRE(clock.total_step_count() == 1U);

    // F5 resumes after main resets its time baseline. The old paused elapsed
    // cannot create catch-up ticks; only the new fixed-step interval advances.
    clock.set_paused(false);
    REQUIRE_FALSE(clock.paused());
    const auto resumed_frame =
        clock.advance(clock.fixed_step_ceiling_duration());
    REQUIRE(resumed_frame);
    REQUIRE(resumed_frame.value().step_count == 1U);
    REQUIRE(clock.total_step_count() == 2U);

    const auto resumed_command =
        player_source.sample_fixed_tick();
    const auto resumed_boom =
        distance_source.sample_fixed_tick();
    const world::ThirdPersonOrbitDelta resumed_delta{
        .yaw_radians =
            resumed_command.look_yaw_delta_radians,
        .pitch_radians =
            resumed_command.look_pitch_delta_radians,
        .boom_distance = resumed_boom,
    };
    REQUIRE(resumed_delta.yaw_radians ==
        Catch::Approx(-0.05F).margin(0.000001F));
    REQUIRE(resumed_delta.pitch_radians ==
        Catch::Approx(-0.05F).margin(0.000001F));
    REQUIRE(resumed_delta.boom_distance ==
        Catch::Approx(1.5F).margin(0.000001F));
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        resumed_delta,
        2U));
    REQUIRE(rig.current.consumed_delta == resumed_delta);

    const auto next_frame =
        clock.advance(clock.fixed_step_ceiling_duration());
    REQUIRE(next_frame);
    REQUIRE(next_frame.value().step_count == 1U);
    REQUIRE(clock.total_step_count() == 3U);
    const auto next_command =
        player_source.sample_fixed_tick();
    const world::ThirdPersonOrbitDelta next_delta{
        .yaw_radians =
            next_command.look_yaw_delta_radians,
        .pitch_radians =
            next_command.look_pitch_delta_radians,
        .boom_distance =
            distance_source.sample_fixed_tick(),
    };
    REQUIRE(next_delta == world::ThirdPersonOrbitDelta{});
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        next_delta,
        3U));
    REQUIRE(rig.current.consumed_delta ==
        world::ThirdPersonOrbitDelta{});
    REQUIRE(rig.current.state == rig.previous.state);
}
