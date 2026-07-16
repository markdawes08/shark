#pragma once

#include <shark/platform/events.hpp>
#include <shark/world/camera.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace shark::sandbox {

struct CameraControllerConfig final {
    float movement_speed{4.0F};
    float sprint_multiplier{4.0F};
    float mouse_sensitivity{0.0025F};
    float maximum_delta_seconds{0.1F};
};

class CameraController final {
public:
    explicit CameraController(
        CameraControllerConfig config = {}) noexcept;

    void handle_event(const platform::Event& event) noexcept;
    void set_focused(bool focused) noexcept;
    void reset() noexcept;
    void update(world::Camera& camera, float delta_seconds) noexcept;

private:
    static constexpr std::size_t key_capacity = 256;

    void handle_key(const platform::KeyEvent& event) noexcept;
    void handle_mouse_move(
        const platform::MouseMovedEvent& event) noexcept;
    void handle_mouse_button(
        const platform::MouseButtonEvent& event) noexcept;
    void clear_input_state() noexcept;

    [[nodiscard]] bool key_down(std::uint32_t virtual_key) const noexcept;

    CameraControllerConfig config_;
    std::array<bool, key_capacity> keys_{};
    bool focused_{true};
    bool minimized_{false};
    bool closed_{false};
    bool right_mouse_down_{false};
    bool mouse_anchor_valid_{false};
    std::int32_t mouse_x_{};
    std::int32_t mouse_y_{};
    float pending_yaw_{};
    float pending_pitch_{};
};

} // namespace shark::sandbox
