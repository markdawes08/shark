#pragma once

#include <shark/character/player_capsule.hpp>
#include <shark/platform/events.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace shark::sandbox {

struct PlayerCommandSourceConfig final {
    float mouse_sensitivity{0.0025F};
};

// Translates device-specific platform events into one device-neutral command
// per fixed simulation tick. Event handling never consumes pending input;
// sample_fixed_tick is the sole consuming boundary.
class PlayerCommandSource final {
public:
    explicit PlayerCommandSource(
        PlayerCommandSourceConfig config = {}) noexcept;

    void handle_event(const platform::Event& event) noexcept;
    void set_focused(bool focused) noexcept;

    // Clears held actions, pending one-shot actions, and accumulated look
    // without changing the current focus/minimize/close lifecycle state.
    void reset() noexcept;

    // Held actions remain active across samples. One-shot actions and mouse
    // look are consumed by the first sample and are neutral thereafter until
    // another platform event arrives.
    [[nodiscard]] character::PlayerActionCommand
        sample_fixed_tick() noexcept;

private:
    static constexpr std::size_t key_capacity = 256;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool key_down(std::uint32_t virtual_key) const noexcept;

    void handle_key(const platform::KeyEvent& event) noexcept;
    void handle_mouse_move(
        const platform::MouseMovedEvent& event) noexcept;
    void handle_mouse_button(
        const platform::MouseButtonEvent& event) noexcept;
    void accumulate_look(double& accumulator, double delta) noexcept;
    void clear_input_state() noexcept;

    PlayerCommandSourceConfig config_;
    std::array<bool, key_capacity> keys_{};
    bool focused_{true};
    bool minimized_{false};
    bool closed_{false};
    bool jump_pending_{false};
    bool primary_action_pending_{false};
    bool reset_pending_{false};
    bool right_mouse_down_{false};
    bool mouse_anchor_valid_{false};
    std::int32_t mouse_x_{};
    std::int32_t mouse_y_{};
    double accumulated_yaw_radians_{};
    double accumulated_pitch_radians_{};
};

} // namespace shark::sandbox
