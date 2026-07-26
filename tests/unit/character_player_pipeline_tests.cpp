#include "player_command_source.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/platform/events.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

inline constexpr std::size_t transcript_tick_count = 120;

struct TranscriptEntry final {
    shark::character::PlayerActionCommand command;
    shark::character::PlayerCapsuleSnapshot previous;
    shark::character::PlayerCapsuleSnapshot current;

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
    case 31:
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
    case 75:
        send_key(source, 'R', platform::KeyAction::pressed);
        send_key(
            source,
            'R',
            platform::KeyAction::pressed,
            true);
        break;
    case 76:
        send_key(source, 'R', platform::KeyAction::released);
        break;
    case 80:
        send_key(source, 'S', platform::KeyAction::pressed);
        send_key(source, 'A', platform::KeyAction::pressed);
        break;
    case 105:
        send_key(source, 'S', platform::KeyAction::released);
        send_key(source, 'A', platform::KeyAction::released);
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
        .spawn_center_position = {0.0F, 3.0F, 0.0F},
        .spawn_facing_yaw_radians = 0.25F,
    }, surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface
make_flat_surface()
{
    auto result = shark::terrain::HeightTileSurface::create({
        .sample_columns = 3U,
        .sample_rows = 3U,
        .sample_spacing = 1.0F,
        .origin = {-1.0F, 0.0F, -1.0F},
        .height_offsets = std::vector<float>(9U, 0.0F),
    });
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] PipelineRun run_pipeline_schedule(
    const std::uint32_t render_rate_hz)
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
    sandbox::PlayerCommandSource command_source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.01F,
        }};

    PipelineRun run;
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
                command_source,
                run.emitted_ticks);
            const auto command =
                command_source.sample_fixed_tick();
            REQUIRE(character::is_valid(command));
            REQUIRE(character::advance_player_capsule(
                player,
                command,
                surface,
                clock.fixed_delta_seconds(),
                run.emitted_ticks));
            REQUIRE(player.current.consumed_command ==
                command);
            run.transcript[
                static_cast<std::size_t>(
                    run.emitted_ticks - 1U)] = {
                        .command = command,
                        .previous = player.previous,
                        .current = player.current,
                    };
        }
    }

    REQUIRE(previous_timestamp == std::chrono::seconds{2});
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
        REQUIRE(run.emitted_ticks == 120U);
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

    std::array<std::uint64_t, 2> landing_ticks{};
    std::size_t landing_count = 0U;
    for (std::size_t entry_index = 0U;
         entry_index < baseline.transcript.size();
         ++entry_index) {
        const auto& snapshot =
            baseline.transcript[entry_index].current;
        if (snapshot.vertical.phase ==
            shark::character::PlayerGroundPhase::landing) {
            REQUIRE(landing_count < landing_ticks.size());
            landing_ticks[landing_count] =
                static_cast<std::uint64_t>(entry_index) + 1U;
            ++landing_count;
            REQUIRE(snapshot.vertical.velocity_y == 0.0F);
            REQUIRE(snapshot.vertical.support_normal ==
                shark::math::Float3{0.0F, 1.0F, 0.0F});
            REQUIRE(snapshot.state.center_position.y == 1.0F);
        }
    }
    REQUIRE(landing_count == landing_ticks.size());
    REQUIRE(landing_ticks[0] == 38U);
    REQUIRE(landing_ticks[1] == 113U);
    REQUIRE(
        baseline.transcript[
            static_cast<std::size_t>(landing_ticks[0])]
            .current.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);
    REQUIRE(
        baseline.transcript[
            static_cast<std::size_t>(landing_ticks[1])]
            .current.vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);

    const auto& reset_tick = baseline.transcript[74];
    REQUIRE(reset_tick.command.reset_pressed);
    REQUIRE(reset_tick.previous.fixed_tick == 74U);
    REQUIRE(reset_tick.current.fixed_tick == 75U);
    REQUIRE(reset_tick.previous.reset_generation == 1U);
    REQUIRE(reset_tick.current.reset_generation == 1U);
    REQUIRE(reset_tick.previous.state ==
        reset_tick.current.state);
    REQUIRE(reset_tick.previous.vertical ==
        reset_tick.current.vertical);
    REQUIRE(reset_tick.current.vertical.phase ==
        shark::character::PlayerGroundPhase::falling);
    REQUIRE(reset_tick.current.vertical.velocity_y == 0.0F);
    REQUIRE(reset_tick.current.state.center_position.y == 3.0F);
    REQUIRE_FALSE(
        baseline.transcript[75].command.reset_pressed);

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
}
