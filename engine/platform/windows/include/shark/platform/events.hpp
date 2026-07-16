#pragma once

#include <cstdint>
#include <variant>

namespace shark::platform {

struct WindowExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend bool operator==(
        const WindowExtent&,
        const WindowExtent&) = default;
};

struct WindowCloseRequestedEvent final {
};

struct WindowClosedEvent final {
};

struct WindowResizedEvent final {
    WindowExtent client_extent;
};

struct WindowMinimizedEvent final {
};

struct WindowRestoredEvent final {
    WindowExtent client_extent;
};

struct WindowFocusChangedEvent final {
    bool focused;
};

enum class KeyAction : std::uint8_t {
    pressed = 1,
    released,
};

struct KeyEvent final {
    std::uint32_t virtual_key;
    std::uint16_t repeat_count;
    std::uint16_t scan_code;
    KeyAction action;
    bool extended;
    bool repeated;
    bool system_key;
};

struct MouseMovedEvent final {
    std::int32_t x;
    std::int32_t y;
};

enum class MouseButton : std::uint8_t {
    left = 1,
    right,
    middle,
    extra_one,
    extra_two,
};

enum class ButtonAction : std::uint8_t {
    pressed = 1,
    released,
};

struct MouseButtonEvent final {
    std::int32_t x;
    std::int32_t y;
    MouseButton button;
    ButtonAction action;
};

enum class MouseWheelAxis : std::uint8_t {
    vertical = 1,
    horizontal,
};

struct MouseWheelEvent final {
    std::int32_t x;
    std::int32_t y;
    std::int32_t delta;
    MouseWheelAxis axis;
};

using Event = std::variant<
    WindowCloseRequestedEvent,
    WindowClosedEvent,
    WindowResizedEvent,
    WindowMinimizedEvent,
    WindowRestoredEvent,
    WindowFocusChangedEvent,
    KeyEvent,
    MouseMovedEvent,
    MouseButtonEvent,
    MouseWheelEvent>;

} // namespace shark::platform
