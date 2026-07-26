#include "player_command_source.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace shark::sandbox {
namespace {

[[nodiscard]] float positive_or_default(
    const float value,
    const float fallback) noexcept
{
    return std::isfinite(value) && value > 0.0F
        ? value
        : fallback;
}

[[nodiscard]] float canonical_zero(const float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

} // namespace

PlayerCommandSource::PlayerCommandSource(
    const PlayerCommandSourceConfig config) noexcept
    : config_{
        .mouse_sensitivity = positive_or_default(
            config.mouse_sensitivity,
            PlayerCommandSourceConfig{}.mouse_sensitivity),
    }
{
}

void PlayerCommandSource::handle_event(
    const platform::Event& event) noexcept
{
    std::visit(
        [this](const auto& typed_event) noexcept {
            using EventType = std::remove_cvref_t<decltype(typed_event)>;
            if constexpr (std::is_same_v<
                              EventType,
                              platform::WindowFocusChangedEvent>) {
                set_focused(typed_event.focused);
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::KeyEvent>) {
                handle_key(typed_event);
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::MouseMovedEvent>) {
                handle_mouse_move(typed_event);
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::MouseButtonEvent>) {
                handle_mouse_button(typed_event);
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::WindowMinimizedEvent>) {
                minimized_ = true;
                clear_input_state();
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::WindowRestoredEvent>) {
                minimized_ = false;
            }
            else if constexpr (
                std::is_same_v<
                    EventType,
                    platform::WindowCloseRequestedEvent> ||
                std::is_same_v<
                    EventType,
                    platform::WindowClosedEvent>) {
                closed_ = true;
                clear_input_state();
            }
        },
        event);
}

void PlayerCommandSource::set_focused(const bool focused) noexcept
{
    focused_ = focused;
    if (!focused_) {
        clear_input_state();
    }
}

void PlayerCommandSource::reset() noexcept
{
    clear_input_state();
}

character::PlayerActionCommand
PlayerCommandSource::sample_fixed_tick() noexcept
{
    if (!active()) {
        clear_input_state();
        return {};
    }

    character::PlayerActionCommand command{
        .move_forward_held = key_down('W'),
        .move_backward_held = key_down('S'),
        .move_left_held = key_down('A'),
        .move_right_held = key_down('D'),
        .run_held =
            key_down(VK_SHIFT) ||
            key_down(VK_LSHIFT) ||
            key_down(VK_RSHIFT),
        .jump_pressed = jump_pending_,
        .primary_action_pressed = primary_action_pending_,
        .reset_pressed = reset_pending_,
        .look_yaw_delta_radians = canonical_zero(
            static_cast<float>(accumulated_yaw_radians_)),
        .look_pitch_delta_radians = canonical_zero(
            static_cast<float>(accumulated_pitch_radians_)),
    };

    jump_pending_ = false;
    primary_action_pending_ = false;
    reset_pending_ = false;
    accumulated_yaw_radians_ = 0.0;
    accumulated_pitch_radians_ = 0.0;
    return command;
}

bool PlayerCommandSource::active() const noexcept
{
    return focused_ && !minimized_ && !closed_;
}

bool PlayerCommandSource::key_down(
    const std::uint32_t virtual_key) const noexcept
{
    return virtual_key < keys_.size() && keys_[virtual_key];
}

void PlayerCommandSource::handle_key(
    const platform::KeyEvent& event) noexcept
{
    if (!active() || event.virtual_key >= keys_.size()) {
        return;
    }

    const auto pressed =
        event.action == platform::KeyAction::pressed;
    keys_[event.virtual_key] = pressed;
    if (!pressed || event.repeated) {
        return;
    }

    if (event.virtual_key == VK_SPACE) {
        jump_pending_ = true;
    }
    else if (event.virtual_key == 'R') {
        reset_pending_ = true;
    }
}

void PlayerCommandSource::handle_mouse_move(
    const platform::MouseMovedEvent& event) noexcept
{
    if (!active() || !right_mouse_down_) {
        return;
    }
    if (!mouse_anchor_valid_) {
        mouse_x_ = event.x;
        mouse_y_ = event.y;
        mouse_anchor_valid_ = true;
        return;
    }

    const auto delta_x =
        static_cast<std::int64_t>(event.x) -
        static_cast<std::int64_t>(mouse_x_);
    const auto delta_y =
        static_cast<std::int64_t>(event.y) -
        static_cast<std::int64_t>(mouse_y_);
    mouse_x_ = event.x;
    mouse_y_ = event.y;

    const auto sensitivity =
        static_cast<double>(config_.mouse_sensitivity);
    accumulate_look(
        accumulated_yaw_radians_,
        static_cast<double>(delta_x) * sensitivity);
    accumulate_look(
        accumulated_pitch_radians_,
        -static_cast<double>(delta_y) * sensitivity);
}

void PlayerCommandSource::handle_mouse_button(
    const platform::MouseButtonEvent& event) noexcept
{
    if (!active()) {
        clear_input_state();
        return;
    }

    const auto pressed =
        event.action == platform::ButtonAction::pressed;
    if (event.button == platform::MouseButton::left) {
        if (pressed) {
            primary_action_pending_ = true;
        }
        return;
    }
    if (event.button != platform::MouseButton::right) {
        return;
    }

    right_mouse_down_ = pressed;
    mouse_anchor_valid_ = pressed;
    mouse_x_ = event.x;
    mouse_y_ = event.y;
}

void PlayerCommandSource::accumulate_look(
    double& accumulator,
    const double delta) noexcept
{
    const auto limit = static_cast<double>(
        character::maximum_player_look_delta_radians);
    accumulator = std::clamp(
        accumulator + delta,
        -limit,
        limit);
    if (accumulator == 0.0) {
        accumulator = 0.0;
    }
}

void PlayerCommandSource::clear_input_state() noexcept
{
    keys_.fill(false);
    jump_pending_ = false;
    primary_action_pending_ = false;
    reset_pending_ = false;
    right_mouse_down_ = false;
    mouse_anchor_valid_ = false;
    accumulated_yaw_radians_ = 0.0;
    accumulated_pitch_radians_ = 0.0;
}

} // namespace shark::sandbox
