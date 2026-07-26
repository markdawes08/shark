#pragma once

#include <shark/platform/events.hpp>

namespace shark::sandbox {

struct CameraDistanceInputSourceConfig final {
    float meters_per_wheel_detent{1.0F};
    float maximum_absolute_delta_meters{16.0F};
};

// Translates vertical mouse-wheel events into one signed camera-distance
// adjustment per fixed simulation tick. Wheel up produces a negative delta
// (zoom in); wheel down produces a positive delta (zoom out).
class CameraDistanceInputSource final {
public:
    explicit CameraDistanceInputSource(
        CameraDistanceInputSourceConfig config = {}) noexcept;

    void handle_event(const platform::Event& event) noexcept;
    void set_focused(bool focused) noexcept;

    // Clears pending wheel input without changing the current
    // focus/minimize/close lifecycle state.
    void reset() noexcept;

    // Event handling never consumes pending input. The first fixed-tick sample
    // returns and consumes the bounded accumulated distance adjustment.
    [[nodiscard]] float sample_fixed_tick() noexcept;

private:
    [[nodiscard]] bool active() const noexcept;
    void handle_mouse_wheel(
        const platform::MouseWheelEvent& event) noexcept;
    void clear_pending_input() noexcept;

    CameraDistanceInputSourceConfig config_;
    bool focused_{true};
    bool minimized_{false};
    bool closed_{false};
    double accumulated_distance_delta_meters_{};
};

} // namespace shark::sandbox
