#include "player_command_source.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/platform/events.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

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
    const std::int32_t x = 0,
    const std::int32_t y = 0) noexcept
{
    source.handle_event(shark::platform::MouseButtonEvent{
        .x = x,
        .y = y,
        .button = button,
        .action = action,
    });
}

void require_neutral(
    const shark::character::PlayerActionCommand& command)
{
    REQUIRE(command ==
        shark::character::PlayerActionCommand{});
    REQUIRE_FALSE(std::signbit(
        command.look_yaw_delta_radians));
    REQUIRE_FALSE(std::signbit(
        command.look_pitch_delta_radians));
}

void populate_pending_input(
    shark::sandbox::PlayerCommandSource& source)
{
    using namespace shark;

    send_key(source, 'W', platform::KeyAction::pressed);
    send_key(source, VK_SPACE, platform::KeyAction::pressed);
    send_key(source, 'R', platform::KeyAction::pressed);
    send_mouse_button(
        source,
        platform::MouseButton::left,
        platform::ButtonAction::pressed);
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::pressed,
        10,
        10);
    source.handle_event(platform::MouseMovedEvent{20, 5});
}

} // namespace

TEST_CASE(
    "player command source samples held actions and consumes tick pulses",
    "[sandbox][player-command][sampling]")
{
    using namespace shark;

    sandbox::PlayerCommandSource source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.01F,
        }};

    for (const auto key : {'W', 'S', 'A', 'D'}) {
        send_key(source, key, platform::KeyAction::pressed);
    }
    send_key(source, VK_LSHIFT, platform::KeyAction::pressed);
    send_key(source, VK_RSHIFT, platform::KeyAction::pressed);
    send_key(source, VK_LSHIFT, platform::KeyAction::released);
    send_key(source, VK_SPACE, platform::KeyAction::pressed);
    send_key(
        source,
        VK_SPACE,
        platform::KeyAction::pressed,
        true);
    send_key(source, 'R', platform::KeyAction::pressed);
    send_key(
        source,
        'R',
        platform::KeyAction::pressed,
        true);
    send_mouse_button(
        source,
        platform::MouseButton::left,
        platform::ButtonAction::pressed);
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::pressed,
        100,
        100);
    source.handle_event(platform::MouseMovedEvent{110, 90});
    source.handle_event(platform::MouseMovedEvent{115, 95});
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::released,
        115,
        95);

    const auto first = source.sample_fixed_tick();
    REQUIRE(first.move_forward_held);
    REQUIRE(first.move_backward_held);
    REQUIRE(first.move_left_held);
    REQUIRE(first.move_right_held);
    REQUIRE(first.run_held);
    REQUIRE(first.jump_pressed);
    REQUIRE(first.primary_action_pressed);
    REQUIRE(first.reset_pressed);
    REQUIRE(first.look_yaw_delta_radians ==
        Catch::Approx(0.15F).margin(0.000001F));
    REQUIRE(first.look_pitch_delta_radians ==
        Catch::Approx(0.05F).margin(0.000001F));
    REQUIRE(character::is_valid(first));

    const auto second = source.sample_fixed_tick();
    REQUIRE(second.move_forward_held);
    REQUIRE(second.move_backward_held);
    REQUIRE(second.move_left_held);
    REQUIRE(second.move_right_held);
    REQUIRE(second.run_held);
    REQUIRE_FALSE(second.jump_pressed);
    REQUIRE_FALSE(second.primary_action_pressed);
    REQUIRE_FALSE(second.reset_pressed);
    REQUIRE(second.look_yaw_delta_radians == 0.0F);
    REQUIRE(second.look_pitch_delta_radians == 0.0F);

    for (const auto key : {'W', 'S', 'A', 'D'}) {
        send_key(source, key, platform::KeyAction::released);
    }
    send_key(source, VK_RSHIFT, platform::KeyAction::released);
    require_neutral(source.sample_fixed_tick());
}

TEST_CASE(
    "player command source retains pending input until a fixed tick",
    "[sandbox][player-command][sampling][boundary]")
{
    using namespace shark;

    sandbox::PlayerCommandSource source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity = 0.02F,
        }};
    send_key(source, 'W', platform::KeyAction::pressed);
    send_key(source, VK_SPACE, platform::KeyAction::pressed);
    send_mouse_button(
        source,
        platform::MouseButton::left,
        platform::ButtonAction::pressed);
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::pressed,
        20,
        20);
    source.handle_event(platform::MouseMovedEvent{25, 15});

    // No API other than sample_fixed_tick consumes this pending state. This
    // models any number of render frames for which the clock emits zero ticks.
    const auto first_tick = source.sample_fixed_tick();
    REQUIRE(first_tick.move_forward_held);
    REQUIRE(first_tick.jump_pressed);
    REQUIRE(first_tick.primary_action_pressed);
    REQUIRE(first_tick.look_yaw_delta_radians ==
        Catch::Approx(0.1F).margin(0.000001F));
    REQUIRE(first_tick.look_pitch_delta_radians ==
        Catch::Approx(0.1F).margin(0.000001F));

    // A catch-up frame may immediately ask for another command. Held input
    // repeats, while pulses and accumulated look were consumed exactly once.
    const auto second_tick = source.sample_fixed_tick();
    REQUIRE(second_tick.move_forward_held);
    REQUIRE_FALSE(second_tick.jump_pressed);
    REQUIRE_FALSE(second_tick.primary_action_pressed);
    REQUIRE(second_tick.look_yaw_delta_radians == 0.0F);
    REQUIRE(second_tick.look_pitch_delta_radians == 0.0F);
}

TEST_CASE(
    "player command source ignores repeated edge presses",
    "[sandbox][player-command][repeat]")
{
    using namespace shark;

    sandbox::PlayerCommandSource source;
    send_key(
        source,
        VK_SPACE,
        platform::KeyAction::pressed,
        true);
    send_key(
        source,
        'R',
        platform::KeyAction::pressed,
        true);
    send_key(
        source,
        'W',
        platform::KeyAction::pressed,
        true);

    const auto repeated_only = source.sample_fixed_tick();
    REQUIRE(repeated_only.move_forward_held);
    REQUIRE_FALSE(repeated_only.jump_pressed);
    REQUIRE_FALSE(repeated_only.reset_pressed);

    send_key(source, VK_SPACE, platform::KeyAction::pressed);
    send_key(source, VK_SPACE, platform::KeyAction::pressed, true);
    send_key(source, 'R', platform::KeyAction::pressed);
    send_key(source, 'R', platform::KeyAction::pressed, true);
    const auto genuine_edges = source.sample_fixed_tick();
    REQUIRE(genuine_edges.jump_pressed);
    REQUIRE(genuine_edges.reset_pressed);
    REQUIRE_FALSE(source.sample_fixed_tick().jump_pressed);
    REQUIRE_FALSE(source.sample_fixed_tick().reset_pressed);
}

TEST_CASE(
    "player command source clears input at lifecycle boundaries",
    "[sandbox][player-command][lifecycle]")
{
    using namespace shark;

    SECTION("focus loss")
    {
        sandbox::PlayerCommandSource source;
        populate_pending_input(source);
        source.handle_event(
            platform::WindowFocusChangedEvent{false});
        require_neutral(source.sample_fixed_tick());

        source.handle_event(
            platform::WindowFocusChangedEvent{true});
        send_key(source, 'D', platform::KeyAction::pressed);
        REQUIRE(source.sample_fixed_tick().move_right_held);
    }

    SECTION("minimize and restore")
    {
        sandbox::PlayerCommandSource source;
        populate_pending_input(source);
        source.handle_event(platform::WindowMinimizedEvent{});
        require_neutral(source.sample_fixed_tick());

        source.handle_event(platform::WindowRestoredEvent{
            platform::WindowExtent{1280, 720}});
        send_key(source, 'A', platform::KeyAction::pressed);
        REQUIRE(source.sample_fixed_tick().move_left_held);
    }

    SECTION("close request is terminal")
    {
        sandbox::PlayerCommandSource source;
        populate_pending_input(source);
        source.handle_event(
            platform::WindowCloseRequestedEvent{});
        send_key(source, 'S', platform::KeyAction::pressed);
        require_neutral(source.sample_fixed_tick());
    }

    SECTION("closed is terminal")
    {
        sandbox::PlayerCommandSource source;
        populate_pending_input(source);
        source.handle_event(platform::WindowClosedEvent{});
        require_neutral(source.sample_fixed_tick());
    }

    SECTION("explicit reset")
    {
        sandbox::PlayerCommandSource source;
        populate_pending_input(source);
        source.reset();
        require_neutral(source.sample_fixed_tick());

        send_key(source, 'W', platform::KeyAction::pressed);
        REQUIRE(source.sample_fixed_tick().move_forward_held);
    }
}

TEST_CASE(
    "player command source bounds overflow-safe mouse accumulation",
    "[sandbox][player-command][mouse][bounds]")
{
    using namespace shark;

    sandbox::PlayerCommandSource source{
        sandbox::PlayerCommandSourceConfig{
            .mouse_sensitivity =
                std::numeric_limits<float>::max(),
        }};
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::pressed,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min());
    source.handle_event(platform::MouseMovedEvent{
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::max(),
    });

    const auto saturated = source.sample_fixed_tick();
    REQUIRE(saturated.look_yaw_delta_radians ==
        character::maximum_player_look_delta_radians);
    REQUIRE(saturated.look_pitch_delta_radians ==
        -character::maximum_player_look_delta_radians);
    REQUIRE(character::is_valid(saturated));
    require_neutral(source.sample_fixed_tick());

    // Motion without a held right button never becomes a tick command.
    send_mouse_button(
        source,
        platform::MouseButton::right,
        platform::ButtonAction::released);
    source.handle_event(platform::MouseMovedEvent{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min(),
    });
    require_neutral(source.sample_fixed_tick());
}
