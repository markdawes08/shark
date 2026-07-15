#include <shark/core/error.hpp>
#include <shark/platform/application.hpp>
#include <shark/platform/events.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

[[nodiscard]] constexpr LPARAM pack_coordinates(
    const std::int16_t x,
    const std::int16_t y) noexcept
{
    const auto low = static_cast<std::uint16_t>(x);
    const auto high = static_cast<std::uint16_t>(y);
    return static_cast<LPARAM>(
        static_cast<std::uint32_t>(low) |
        (static_cast<std::uint32_t>(high) << 16U));
}

[[nodiscard]] constexpr WPARAM pack_wheel_delta(
    const std::int16_t delta) noexcept
{
    return static_cast<WPARAM>(
        static_cast<std::uint32_t>(
            static_cast<std::uint16_t>(delta)) << 16U);
}

} // namespace

TEST_CASE("invalid window configuration returns platform errors", "[platform][window]")
{
    using namespace shark;

    SECTION("zero client extent")
    {
        platform::ApplicationConfig config;
        config.client_extent.width = 0;
        config.visible = false;

        const auto result = platform::Application::create(config);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().category() == core::ErrorCategory::platform);
        REQUIRE(result.error().code() == core::ErrorCode::invalid_argument);
    }

    SECTION("invalid UTF-8 title")
    {
        platform::ApplicationConfig config;
        config.title = std::string{
            static_cast<char>(0xc3),
            static_cast<char>(0x28),
        };
        config.visible = false;

        const auto result = platform::Application::create(config);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().category() == core::ErrorCategory::platform);
        REQUIRE(result.error().code() == core::ErrorCode::invalid_argument);
    }
}

TEST_CASE(
    "window client extents are physical pixels under per-monitor DPI v2",
    "[platform][window][dpi]")
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.client_extent = platform::WindowExtent{800, 600};
    config.visible = false;
    auto application_result = platform::Application::create(config);
    REQUIRE(application_result.has_value());
    auto application = std::move(application_result).value();

    const auto window = static_cast<HWND>(
        application.native_window_handle().value);
    REQUIRE(window != nullptr);
    REQUIRE(AreDpiAwarenessContextsEqual(
        GetWindowDpiAwarenessContext(window),
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    REQUIRE(GetDpiForWindow(window) != 0);

    RECT initial_client{};
    REQUIRE(GetClientRect(window, &initial_client) != FALSE);
    const platform::WindowExtent observed_initial{
        static_cast<std::uint32_t>(
            initial_client.right - initial_client.left),
        static_cast<std::uint32_t>(
            initial_client.bottom - initial_client.top),
    };
    REQUIRE(observed_initial == config.client_extent);
    REQUIRE(application.client_extent() == observed_initial);

    application.clear_events();
    constexpr platform::WindowExtent resized_extent{960, 540};
    REQUIRE(application.resize_client(resized_extent).has_value());

    RECT resized_client{};
    REQUIRE(GetClientRect(window, &resized_client) != FALSE);
    const platform::WindowExtent observed_resized{
        static_cast<std::uint32_t>(
            resized_client.right - resized_client.left),
        static_cast<std::uint32_t>(
            resized_client.bottom - resized_client.top),
    };
    REQUIRE(observed_resized == resized_extent);
    REQUIRE(application.client_extent() == observed_resized);

    bool published_physical_resize = false;
    for (const auto& event : application.events()) {
        const auto* const resized =
            std::get_if<platform::WindowResizedEvent>(&event);
        if (resized != nullptr &&
            resized->client_extent == observed_resized) {
            published_physical_resize = true;
        }
    }
    REQUIRE(published_physical_resize);

    application.clear_events();
    RECT current_window{};
    REQUIRE(GetWindowRect(window, &current_window) != FALSE);
    const auto current_width = current_window.right - current_window.left;
    const auto current_height = current_window.bottom - current_window.top;
    RECT suggested_window{
        .left = current_window.left + 11,
        .top = current_window.top + 13,
        .right = current_window.left + 11 + current_width + 64,
        .bottom = current_window.top + 13 + current_height + 32,
    };
    const auto dpi = static_cast<WORD>(GetDpiForWindow(window));
    static_cast<void>(SendMessageW(
        window,
        WM_DPICHANGED,
        static_cast<WPARAM>(MAKELONG(dpi, dpi)),
        reinterpret_cast<LPARAM>(&suggested_window)));

    const auto pump_result = application.poll_events();
    REQUIRE(pump_result.has_value());
    RECT dpi_changed_window{};
    REQUIRE(GetWindowRect(window, &dpi_changed_window) != FALSE);
    REQUIRE(dpi_changed_window.left == suggested_window.left);
    REQUIRE(dpi_changed_window.top == suggested_window.top);
    REQUIRE(dpi_changed_window.right == suggested_window.right);
    REQUIRE(dpi_changed_window.bottom == suggested_window.bottom);

    RECT dpi_changed_client{};
    REQUIRE(GetClientRect(window, &dpi_changed_client) != FALSE);
    const platform::WindowExtent observed_dpi_changed{
        static_cast<std::uint32_t>(
            dpi_changed_client.right - dpi_changed_client.left),
        static_cast<std::uint32_t>(
            dpi_changed_client.bottom - dpi_changed_client.top),
    };
    REQUIRE(application.client_extent() == observed_dpi_changed);
    REQUIRE(application.close_window().has_value());
}

TEST_CASE("Win32 messages cross the platform event boundary in order", "[platform][window][input]")
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.visible = false;
    auto application_result = platform::Application::create(config);
    REQUIRE(application_result.has_value());
    auto application = std::move(application_result).value();
    application.clear_events();

    const auto native_handle = application.native_window_handle();
    REQUIRE(static_cast<bool>(native_handle));
    const auto window = static_cast<HWND>(native_handle.value);
    REQUIRE(IsWindow(window) != FALSE);
    REQUIRE(IsWindowVisible(window) == FALSE);

    static_cast<void>(SendMessageW(
        window,
        WM_SIZE,
        SIZE_RESTORED,
        pack_coordinates(640, 480)));
    static_cast<void>(SendMessageW(
        window,
        WM_SIZE,
        SIZE_MINIMIZED,
        0));
    static_cast<void>(SendMessageW(
        window,
        WM_SIZE,
        SIZE_RESTORED,
        pack_coordinates(800, 600)));

    constexpr LPARAM repeated_key_down =
        static_cast<LPARAM>(
            2U |
            (0x11U << 16U) |
            (1U << 24U) |
            (1U << 30U));
    constexpr LPARAM key_up =
        static_cast<LPARAM>(
            1U |
            (0x11U << 16U) |
            (1U << 24U) |
            (1U << 30U) |
            (1U << 31U));
    static_cast<void>(SendMessageW(
        window,
        WM_KEYDOWN,
        static_cast<WPARAM>('W'),
        repeated_key_down));
    static_cast<void>(SendMessageW(
        window,
        WM_KEYUP,
        static_cast<WPARAM>('W'),
        key_up));

    static_cast<void>(SendMessageW(
        window,
        WM_MOUSEMOVE,
        0,
        pack_coordinates(-12, 34)));
    static_cast<void>(SendMessageW(
        window,
        WM_LBUTTONDOWN,
        MK_LBUTTON,
        pack_coordinates(20, 30)));
    static_cast<void>(SendMessageW(
        window,
        WM_LBUTTONUP,
        0,
        pack_coordinates(20, 30)));
    static_cast<void>(SendMessageW(
        window,
        WM_MOUSEWHEEL,
        pack_wheel_delta(WHEEL_DELTA),
        pack_coordinates(100, 120)));
    static_cast<void>(SendMessageW(window, WM_CLOSE, 0, 0));

    const auto events = application.events();
    REQUIRE(events.size() == 11);

    const auto* resized =
        std::get_if<platform::WindowResizedEvent>(&events[0]);
    REQUIRE(resized != nullptr);
    REQUIRE((resized->client_extent == platform::WindowExtent{640, 480}));
    REQUIRE(std::holds_alternative<
        platform::WindowMinimizedEvent>(events[1]));

    const auto* restored =
        std::get_if<platform::WindowRestoredEvent>(&events[2]);
    REQUIRE(restored != nullptr);
    REQUIRE((restored->client_extent == platform::WindowExtent{800, 600}));
    const auto* resized_after_restore =
        std::get_if<platform::WindowResizedEvent>(&events[3]);
    REQUIRE(resized_after_restore != nullptr);
    REQUIRE((
        resized_after_restore->client_extent ==
        platform::WindowExtent{800, 600}));

    const auto* key_down = std::get_if<platform::KeyEvent>(&events[4]);
    REQUIRE(key_down != nullptr);
    REQUIRE(key_down->virtual_key == static_cast<std::uint32_t>('W'));
    REQUIRE(key_down->action == platform::KeyAction::pressed);
    REQUIRE(key_down->repeat_count == 2);
    REQUIRE(key_down->scan_code == 0x11);
    REQUIRE(key_down->extended);
    REQUIRE(key_down->repeated);
    REQUIRE_FALSE(key_down->system_key);

    const auto* released_key = std::get_if<platform::KeyEvent>(&events[5]);
    REQUIRE(released_key != nullptr);
    REQUIRE(released_key->action == platform::KeyAction::released);
    REQUIRE_FALSE(released_key->repeated);

    const auto* moved =
        std::get_if<platform::MouseMovedEvent>(&events[6]);
    REQUIRE(moved != nullptr);
    REQUIRE(moved->x == -12);
    REQUIRE(moved->y == 34);

    const auto* button_down =
        std::get_if<platform::MouseButtonEvent>(&events[7]);
    REQUIRE(button_down != nullptr);
    REQUIRE(button_down->button == platform::MouseButton::left);
    REQUIRE(button_down->action == platform::ButtonAction::pressed);
    const auto* button_up =
        std::get_if<platform::MouseButtonEvent>(&events[8]);
    REQUIRE(button_up != nullptr);
    REQUIRE(button_up->action == platform::ButtonAction::released);

    const auto* wheel =
        std::get_if<platform::MouseWheelEvent>(&events[9]);
    REQUIRE(wheel != nullptr);
    REQUIRE(wheel->delta == WHEEL_DELTA);
    REQUIRE(wheel->axis == platform::MouseWheelAxis::vertical);

    REQUIRE(std::holds_alternative<
        platform::WindowCloseRequestedEvent>(events[10]));
    REQUIRE(application.running());
    REQUIRE(IsWindow(window) != FALSE);
    REQUIRE((
        application.client_extent() == platform::WindowExtent{800, 600}));
    REQUIRE_FALSE(application.minimized());

    application.clear_events();
    const auto close_result = application.close_window();
    REQUIRE(close_result.has_value());
    REQUIRE_FALSE(application.running());
    REQUIRE_FALSE(static_cast<bool>(application.native_window_handle()));
    REQUIRE(application.events().size() == 1);
    REQUIRE(std::holds_alternative<
        platform::WindowClosedEvent>(application.events().front()));
}

TEST_CASE("the platform event buffer is bounded and observable", "[platform][window]")
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.visible = false;
    auto application_result = platform::Application::create(config);
    REQUIRE(application_result.has_value());
    auto application = std::move(application_result).value();
    application.clear_events();

    const auto window = static_cast<HWND>(
        application.native_window_handle().value);
    constexpr std::size_t sent_event_count = 300;
    for (std::size_t index = 0; index < sent_event_count; ++index) {
        static_cast<void>(SendMessageW(
            window,
            WM_MOUSEMOVE,
            0,
            pack_coordinates(
                static_cast<std::int16_t>(index),
                1)));
    }

    constexpr std::size_t retained_input_capacity = 254;
    REQUIRE(application.events().size() == retained_input_capacity);
    REQUIRE(
        application.dropped_event_count() ==
        sent_event_count - retained_input_capacity);

    REQUIRE(application.request_close().has_value());
    const auto pump_result = application.poll_events();
    REQUIRE(pump_result.has_value());
    REQUIRE(application.events().size() == retained_input_capacity + 1);
    REQUIRE(std::holds_alternative<
        platform::WindowCloseRequestedEvent>(application.events().back()));
    REQUIRE(
        application.dropped_event_count() ==
        sent_event_count - retained_input_capacity);

    REQUIRE(application.close_window().has_value());
    REQUIRE(application.events().size() == retained_input_capacity + 2);
    REQUIRE(std::holds_alternative<
        platform::WindowClosedEvent>(application.events().back()));
    REQUIRE(
        application.dropped_event_count() ==
        sent_event_count - retained_input_capacity);

    application.clear_events();
    REQUIRE(application.events().empty());
    REQUIRE(application.dropped_event_count() == 0);
    REQUIRE_FALSE(application.running());
}

TEST_CASE("the message pump reports WM_QUIT without leaking the window", "[platform][window]")
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.visible = false;
    auto application_result = platform::Application::create(config);
    REQUIRE(application_result.has_value());
    auto application = std::move(application_result).value();

    PostQuitMessage(7);
    const auto pump_result = application.poll_events();
    REQUIRE(pump_result.has_value());
    REQUIRE(pump_result.value().quit_requested);
    REQUIRE(pump_result.value().exit_code == 7);
    REQUIRE_FALSE(application.running());
    REQUIRE(application.close_window().has_value());
    REQUIRE_FALSE(static_cast<bool>(application.native_window_handle()));
}

TEST_CASE("message pumping rejects the wrong thread", "[platform][window][threading]")
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.visible = false;
    auto application_result = platform::Application::create(config);
    REQUIRE(application_result.has_value());
    auto application = std::move(application_result).value();

    std::optional<core::ErrorCode> observed_error;
    std::thread worker([&application, &observed_error] {
        const auto poll_result = application.poll_events();
        if (!poll_result) {
            observed_error.emplace(poll_result.error().code());
        }
    });
    worker.join();

    REQUIRE(observed_error.has_value());
    REQUIRE(*observed_error == core::ErrorCode::invalid_state);
    REQUIRE(application.close_window().has_value());
}
