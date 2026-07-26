#include "camera_distance_input_source.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace shark::sandbox {
namespace {

constexpr double wheel_delta_per_detent = 120.0;

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

CameraDistanceInputSource::CameraDistanceInputSource(
    const CameraDistanceInputSourceConfig config) noexcept
    : config_{
        .meters_per_wheel_detent = positive_or_default(
            config.meters_per_wheel_detent,
            CameraDistanceInputSourceConfig{}
                .meters_per_wheel_detent),
        .maximum_absolute_delta_meters = positive_or_default(
            config.maximum_absolute_delta_meters,
            CameraDistanceInputSourceConfig{}
                .maximum_absolute_delta_meters),
    }
{
}

void CameraDistanceInputSource::handle_event(
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
                                   platform::MouseWheelEvent>) {
                handle_mouse_wheel(typed_event);
            }
            else if constexpr (std::is_same_v<
                                   EventType,
                                   platform::WindowMinimizedEvent>) {
                minimized_ = true;
                clear_pending_input();
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
                clear_pending_input();
            }
        },
        event);
}

void CameraDistanceInputSource::set_focused(
    const bool focused) noexcept
{
    focused_ = focused;
    if (!focused_) {
        clear_pending_input();
    }
}

void CameraDistanceInputSource::reset() noexcept
{
    clear_pending_input();
}

float CameraDistanceInputSource::sample_fixed_tick() noexcept
{
    if (!active()) {
        clear_pending_input();
        return 0.0F;
    }

    const auto distance_delta = canonical_zero(
        static_cast<float>(
            accumulated_distance_delta_meters_));
    clear_pending_input();
    return distance_delta;
}

bool CameraDistanceInputSource::active() const noexcept
{
    return focused_ && !minimized_ && !closed_;
}

void CameraDistanceInputSource::handle_mouse_wheel(
    const platform::MouseWheelEvent& event) noexcept
{
    if (!active() ||
        event.axis != platform::MouseWheelAxis::vertical ||
        event.delta == 0) {
        return;
    }

    const auto distance_delta =
        -static_cast<double>(event.delta) /
        wheel_delta_per_detent *
        static_cast<double>(config_.meters_per_wheel_detent);
    const auto limit = static_cast<double>(
        config_.maximum_absolute_delta_meters);
    accumulated_distance_delta_meters_ = std::clamp(
        accumulated_distance_delta_meters_ + distance_delta,
        -limit,
        limit);
    if (accumulated_distance_delta_meters_ == 0.0) {
        accumulated_distance_delta_meters_ = 0.0;
    }
}

void CameraDistanceInputSource::clear_pending_input() noexcept
{
    accumulated_distance_delta_meters_ = 0.0;
}

} // namespace shark::sandbox
