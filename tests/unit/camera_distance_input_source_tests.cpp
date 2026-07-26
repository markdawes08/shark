#include "camera_distance_input_source.hpp"

#include <shark/platform/events.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

void send_wheel(
    shark::sandbox::CameraDistanceInputSource& source,
    const std::int32_t delta,
    const shark::platform::MouseWheelAxis axis =
        shark::platform::MouseWheelAxis::vertical) noexcept
{
    source.handle_event(shark::platform::MouseWheelEvent{
        .x = 40,
        .y = 80,
        .delta = delta,
        .axis = axis,
    });
}

void require_neutral(
    shark::sandbox::CameraDistanceInputSource& source)
{
    const auto sampled = source.sample_fixed_tick();
    REQUIRE(sampled == 0.0F);
    REQUIRE_FALSE(std::signbit(sampled));
}

void populate_pending_input(
    shark::sandbox::CameraDistanceInputSource& source) noexcept
{
    send_wheel(source, 120);
}

} // namespace

TEST_CASE(
    "camera distance input maps vertical wheel to signed meters",
    "[sandbox][camera-distance-input][sampling]")
{
    using namespace shark;

    sandbox::CameraDistanceInputSource source{
        sandbox::CameraDistanceInputSourceConfig{
            .meters_per_wheel_detent = 1.5F,
            .maximum_absolute_delta_meters = 20.0F,
        }};

    send_wheel(source, 240);
    send_wheel(source, -120);
    send_wheel(
        source,
        std::numeric_limits<std::int32_t>::max(),
        platform::MouseWheelAxis::horizontal);
    send_wheel(source, 0);

    // Wheel up zooms in, so two upward detents and one downward detent
    // produce a one-detent negative camera-distance adjustment.
    REQUIRE(source.sample_fixed_tick() ==
        Catch::Approx(-1.5F).margin(0.000001F));
    require_neutral(source);

    send_wheel(source, -120);
    REQUIRE(source.sample_fixed_tick() ==
        Catch::Approx(1.5F).margin(0.000001F));
}

TEST_CASE(
    "camera distance input remains pending until one fixed tick",
    "[sandbox][camera-distance-input][sampling][boundary]")
{
    using namespace shark;

    sandbox::CameraDistanceInputSource source{
        sandbox::CameraDistanceInputSourceConfig{
            .meters_per_wheel_detent = 2.0F,
            .maximum_absolute_delta_meters = 10.0F,
        }};
    send_wheel(source, 120);
    send_wheel(source, 60);

    // No API other than sample_fixed_tick consumes the accumulated wheel
    // input, so it survives any number of render frames that emit zero ticks.
    REQUIRE(source.sample_fixed_tick() ==
        Catch::Approx(-3.0F).margin(0.000001F));

    // A catch-up frame may immediately emit another tick. The wheel delta was
    // consumed exactly once.
    require_neutral(source);
}

TEST_CASE(
    "camera distance input sanitizes invalid configuration",
    "[sandbox][camera-distance-input][config]")
{
    using namespace shark;

    const auto defaults =
        sandbox::CameraDistanceInputSourceConfig{};

    SECTION("non-positive values")
    {
        sandbox::CameraDistanceInputSource source{
            sandbox::CameraDistanceInputSourceConfig{
                .meters_per_wheel_detent = 0.0F,
                .maximum_absolute_delta_meters = -1.0F,
            }};
        send_wheel(source, 120);
        REQUIRE(source.sample_fixed_tick() ==
            -defaults.meters_per_wheel_detent);
    }

    SECTION("non-finite values")
    {
        sandbox::CameraDistanceInputSource source{
            sandbox::CameraDistanceInputSourceConfig{
                .meters_per_wheel_detent =
                    std::numeric_limits<float>::infinity(),
                .maximum_absolute_delta_meters =
                    std::numeric_limits<float>::quiet_NaN(),
            }};
        send_wheel(source, 120);
        REQUIRE(source.sample_fixed_tick() ==
            -defaults.meters_per_wheel_detent);
    }
}

TEST_CASE(
    "camera distance input safely saturates extreme accumulation",
    "[sandbox][camera-distance-input][bounds]")
{
    using namespace shark;

    constexpr auto limit = 3.25F;
    sandbox::CameraDistanceInputSource source{
        sandbox::CameraDistanceInputSourceConfig{
            .meters_per_wheel_detent =
                std::numeric_limits<float>::max(),
            .maximum_absolute_delta_meters = limit,
        }};

    send_wheel(
        source,
        std::numeric_limits<std::int32_t>::max());
    const auto zoom_in = source.sample_fixed_tick();
    REQUIRE(std::isfinite(zoom_in));
    REQUIRE(zoom_in == -limit);

    send_wheel(
        source,
        std::numeric_limits<std::int32_t>::min());
    const auto zoom_out = source.sample_fixed_tick();
    REQUIRE(std::isfinite(zoom_out));
    REQUIRE(zoom_out == limit);

    for (auto event_index = 0; event_index < 1'000;
         ++event_index) {
        send_wheel(source, std::numeric_limits<std::int32_t>::min());
    }
    REQUIRE(source.sample_fixed_tick() == limit);
    require_neutral(source);
}

TEST_CASE(
    "camera distance input clears pending state at lifecycle boundaries",
    "[sandbox][camera-distance-input][lifecycle]")
{
    using namespace shark;

    SECTION("focus loss and recovery")
    {
        sandbox::CameraDistanceInputSource source;
        populate_pending_input(source);
        source.handle_event(
            platform::WindowFocusChangedEvent{false});
        send_wheel(source, 120);
        require_neutral(source);

        source.handle_event(
            platform::WindowFocusChangedEvent{true});
        send_wheel(source, -120);
        REQUIRE(source.sample_fixed_tick() == 1.0F);

        populate_pending_input(source);
        source.set_focused(false);
        require_neutral(source);
        source.set_focused(true);
        send_wheel(source, 120);
        REQUIRE(source.sample_fixed_tick() == -1.0F);
    }

    SECTION("minimize and restore")
    {
        sandbox::CameraDistanceInputSource source;
        populate_pending_input(source);
        source.handle_event(platform::WindowMinimizedEvent{});
        send_wheel(source, 120);
        require_neutral(source);

        source.handle_event(platform::WindowRestoredEvent{
            platform::WindowExtent{1280, 720}});
        send_wheel(source, -120);
        REQUIRE(source.sample_fixed_tick() == 1.0F);
    }

    SECTION("close request is terminal")
    {
        sandbox::CameraDistanceInputSource source;
        populate_pending_input(source);
        source.handle_event(
            platform::WindowCloseRequestedEvent{});
        send_wheel(source, -120);
        require_neutral(source);
        source.handle_event(
            platform::WindowFocusChangedEvent{true});
        source.handle_event(platform::WindowRestoredEvent{
            platform::WindowExtent{1280, 720}});
        send_wheel(source, -120);
        require_neutral(source);
    }

    SECTION("closed is terminal")
    {
        sandbox::CameraDistanceInputSource source;
        populate_pending_input(source);
        source.handle_event(platform::WindowClosedEvent{});
        send_wheel(source, -120);
        require_neutral(source);
    }

    SECTION("explicit reset preserves active lifecycle")
    {
        sandbox::CameraDistanceInputSource source;
        populate_pending_input(source);
        source.reset();
        require_neutral(source);

        send_wheel(source, -120);
        REQUIRE(source.sample_fixed_tick() == 1.0F);
    }
}
