#include "camera_controller.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
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

} // namespace

CameraController::CameraController(
    CameraControllerConfig config) noexcept
    : config_{
        .movement_speed = positive_or_default(
            config.movement_speed,
            CameraControllerConfig{}.movement_speed),
        .sprint_multiplier = positive_or_default(
            config.sprint_multiplier,
            CameraControllerConfig{}.sprint_multiplier),
        .mouse_sensitivity = positive_or_default(
            config.mouse_sensitivity,
            CameraControllerConfig{}.mouse_sensitivity),
        .maximum_delta_seconds = positive_or_default(
            config.maximum_delta_seconds,
            CameraControllerConfig{}.maximum_delta_seconds),
    }
{
}

void CameraController::handle_event(
    const platform::Event& event) noexcept
{
    std::visit(
        [this](const auto& typed_event) noexcept {
            using EventType = std::remove_cvref_t<decltype(typed_event)>;
            if constexpr (std::is_same_v<
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

void CameraController::set_focused(const bool focused) noexcept
{
    focused_ = focused;
    if (!focused_) {
        clear_input_state();
    }
}

void CameraController::reset() noexcept
{
    clear_input_state();
}

void CameraController::update(
    world::Camera& camera,
    const float delta_seconds) noexcept
{
    if (!focused_ || minimized_ || closed_) {
        clear_input_state();
        return;
    }

    constexpr std::uint32_t key_w = 'W';
    constexpr std::uint32_t key_a = 'A';
    constexpr std::uint32_t key_s = 'S';
    constexpr std::uint32_t key_d = 'D';
    constexpr std::uint32_t key_q = 'Q';
    constexpr std::uint32_t key_e = 'E';

    const auto forward =
        static_cast<float>(key_down(key_w)) -
        static_cast<float>(key_down(key_s));
    const auto right =
        static_cast<float>(key_down(key_d)) -
        static_cast<float>(key_down(key_a));
    const auto up =
        static_cast<float>(
            key_down(key_e) || key_down(VK_SPACE)) -
        static_cast<float>(
            key_down(key_q) ||
            key_down(VK_CONTROL) ||
            key_down(VK_LCONTROL) ||
            key_down(VK_RCONTROL));
    const auto sprinting =
        key_down(VK_SHIFT) ||
        key_down(VK_LSHIFT) ||
        key_down(VK_RSHIFT);

    auto bounded_delta = 0.0F;
    if (std::isfinite(delta_seconds) && delta_seconds > 0.0F) {
        bounded_delta = std::min(
            delta_seconds,
            config_.maximum_delta_seconds);
    }

    const auto speed = config_.movement_speed *
        (sprinting ? config_.sprint_multiplier : 1.0F);
    world::advance_camera(
        camera,
        world::CameraMotion{
            .right = right,
            .up = up,
            .forward = forward,
            .yaw_radians = pending_yaw_,
            .pitch_radians = pending_pitch_,
        },
        bounded_delta,
        speed);
    pending_yaw_ = 0.0F;
    pending_pitch_ = 0.0F;
}

void CameraController::handle_key(
    const platform::KeyEvent& event) noexcept
{
    if (!focused_ || minimized_ || closed_ ||
        event.virtual_key >= keys_.size()) {
        return;
    }

    keys_[event.virtual_key] =
        event.action == platform::KeyAction::pressed;
}

void CameraController::handle_mouse_move(
    const platform::MouseMovedEvent& event) noexcept
{
    if (!focused_ || minimized_ || closed_ ||
        !right_mouse_down_) {
        return;
    }
    if (!mouse_anchor_valid_) {
        mouse_x_ = event.x;
        mouse_y_ = event.y;
        mouse_anchor_valid_ = true;
        return;
    }

    const auto delta_x = event.x - mouse_x_;
    const auto delta_y = event.y - mouse_y_;
    mouse_x_ = event.x;
    mouse_y_ = event.y;
    pending_yaw_ +=
        static_cast<float>(delta_x) * config_.mouse_sensitivity;
    pending_pitch_ -=
        static_cast<float>(delta_y) * config_.mouse_sensitivity;
}

void CameraController::handle_mouse_button(
    const platform::MouseButtonEvent& event) noexcept
{
    if (event.button != platform::MouseButton::right) {
        return;
    }
    if (!focused_ || minimized_ || closed_) {
        clear_input_state();
        return;
    }

    right_mouse_down_ =
        event.action == platform::ButtonAction::pressed;
    mouse_anchor_valid_ = right_mouse_down_;
    mouse_x_ = event.x;
    mouse_y_ = event.y;
}

void CameraController::clear_input_state() noexcept
{
    keys_.fill(false);
    right_mouse_down_ = false;
    mouse_anchor_valid_ = false;
    pending_yaw_ = 0.0F;
    pending_pitch_ = 0.0F;
}

bool CameraController::key_down(
    const std::uint32_t virtual_key) const noexcept
{
    return virtual_key < keys_.size() && keys_[virtual_key];
}

} // namespace shark::sandbox
