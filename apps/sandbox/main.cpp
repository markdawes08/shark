#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/core/result.hpp>
#include <shark/platform/application.hpp>
#include <shark/platform/events.hpp>

#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace {

template<typename... Visitors>
struct EventVisitor final : Visitors... {
    using Visitors::operator()...;
};

template<typename... Visitors>
EventVisitor(Visitors...) -> EventVisitor<Visitors...>;

class LoggingSession final {
public:
    LoggingSession() = default;
    ~LoggingSession()
    {
        shark::core::shutdown_logging();
    }

    LoggingSession(const LoggingSession&) = delete;
    LoggingSession& operator=(const LoggingSession&) = delete;
    LoggingSession(LoggingSession&&) = delete;
    LoggingSession& operator=(LoggingSession&&) = delete;
};

[[nodiscard]] std::string format_extent(
    const shark::platform::WindowExtent extent)
{
    auto message = std::to_string(extent.width);
    message.push_back('x');
    message.append(std::to_string(extent.height));
    return message;
}

[[nodiscard]] std::string_view key_action_name(
    const shark::platform::KeyAction action) noexcept
{
    return action == shark::platform::KeyAction::pressed
        ? "pressed"
        : "released";
}

[[nodiscard]] std::string_view button_action_name(
    const shark::platform::ButtonAction action) noexcept
{
    return action == shark::platform::ButtonAction::pressed
        ? "pressed"
        : "released";
}

[[nodiscard]] std::string_view button_name(
    const shark::platform::MouseButton button) noexcept
{
    using shark::platform::MouseButton;
    switch (button) {
    case MouseButton::left:
        return "left";
    case MouseButton::right:
        return "right";
    case MouseButton::middle:
        return "middle";
    case MouseButton::extra_one:
        return "extra-one";
    case MouseButton::extra_two:
        return "extra-two";
    }

    return "unknown";
}

void log_platform_event(const shark::platform::Event& event)
{
    using namespace shark;

    std::visit(
        EventVisitor{
            [](const platform::WindowCloseRequestedEvent&) {
                core::log_message(
                    core::LogLevel::info,
                    "window",
                    "Close requested");
            },
            [](const platform::WindowClosedEvent&) {
                core::log_message(
                    core::LogLevel::info,
                    "window",
                    "Closed");
            },
            [](const platform::WindowResizedEvent& resized) {
                const auto extent = format_extent(resized.client_extent);
                core::log_message(
                    core::LogLevel::info,
                    "window",
                    std::string{"Resized client area to "} + extent);
            },
            [](const platform::WindowMinimizedEvent&) {
                core::log_message(
                    core::LogLevel::info,
                    "window",
                    "Minimized");
            },
            [](const platform::WindowRestoredEvent& restored) {
                const auto extent = format_extent(restored.client_extent);
                core::log_message(
                    core::LogLevel::info,
                    "window",
                    std::string{"Restored client area at "} + extent);
            },
            [](const platform::KeyEvent& key) {
                auto message = std::string{"Virtual key "};
                message.append(std::to_string(key.virtual_key));
                message.push_back(' ');
                message.append(key_action_name(key.action));
                message.append(" (scan=");
                message.append(std::to_string(key.scan_code));
                message.append(", repeat-count=");
                message.append(std::to_string(key.repeat_count));
                message.append(key.repeated ? ", repeated" : ", first-press");
                message.append(key.extended ? ", extended" : ", standard");
                message.append(key.system_key ? ", system)" : ", regular)");
                core::log_message(
                    core::LogLevel::debug,
                    "input.keyboard",
                    message);
            },
            [](const platform::MouseMovedEvent& moved) {
                auto message = std::string{"Moved to ("};
                message.append(std::to_string(moved.x));
                message.append(", ");
                message.append(std::to_string(moved.y));
                message.push_back(')');
                core::log_message(
                    core::LogLevel::debug,
                    "input.mouse",
                    message);
            },
            [](const platform::MouseButtonEvent& button) {
                auto message = std::string{button_name(button.button)};
                message.append(" button ");
                message.append(button_action_name(button.action));
                message.append(" at (");
                message.append(std::to_string(button.x));
                message.append(", ");
                message.append(std::to_string(button.y));
                message.push_back(')');
                core::log_message(
                    core::LogLevel::debug,
                    "input.mouse",
                    message);
            },
            [](const platform::MouseWheelEvent& wheel) {
                auto message = wheel.axis == platform::MouseWheelAxis::vertical
                    ? std::string{"Vertical wheel delta "}
                    : std::string{"Horizontal wheel delta "};
                message.append(std::to_string(wheel.delta));
                message.append(" at (");
                message.append(std::to_string(wheel.x));
                message.append(", ");
                message.append(std::to_string(wheel.y));
                message.push_back(')');
                core::log_message(
                    core::LogLevel::debug,
                    "input.mouse",
                    message);
            },
        },
        event);
}

[[nodiscard]] shark::core::Result<void> run_platform_shell(
    const bool smoke_mode)
{
    using namespace shark;

    platform::ApplicationConfig config;
    config.visible = !smoke_mode;

    auto application_result = platform::Application::create(config);
    if (!application_result) {
        return core::Result<void>::failure(
            std::move(application_result).error());
    }
    auto application = std::move(application_result).value();

    core::log_message(
        core::LogLevel::info,
        "sandbox",
        smoke_mode
            ? "Running Win32 platform smoke test"
            : "Win32 application shell initialized");

    constexpr platform::WindowExtent smoke_extent{960, 540};
    if (smoke_mode) {
        auto show_result = application.show_window();
        if (!show_result) {
            return core::Result<void>::failure(
                std::move(show_result).error());
        }
        auto resize_result = application.resize_client(smoke_extent);
        if (!resize_result) {
            return core::Result<void>::failure(
                std::move(resize_result).error());
        }
        auto minimize_result = application.minimize_window();
        if (!minimize_result) {
            return core::Result<void>::failure(
                std::move(minimize_result).error());
        }
        auto restore_result = application.restore_window();
        if (!restore_result) {
            return core::Result<void>::failure(
                std::move(restore_result).error());
        }
    }

    std::optional<core::Error> asynchronous_close_error;
    std::jthread asynchronous_close;

    bool observed_resize = false;
    bool observed_minimized = false;
    bool observed_restored = false;
    bool observed_close_request = false;
    bool observed_closed = false;
    bool entered_idle_wait = false;
    for (;;) {
        auto pump_result = application.poll_events();
        if (!pump_result) {
            return core::Result<void>::failure(
                std::move(pump_result).error());
        }

        bool accept_close = false;
        for (const auto& event : application.events()) {
            log_platform_event(event);
            if (const auto* resized = std::get_if<
                    platform::WindowResizedEvent>(&event);
                resized != nullptr &&
                resized->client_extent == smoke_extent) {
                observed_resize = true;
            }
            else if (std::holds_alternative<
                         platform::WindowMinimizedEvent>(event)) {
                observed_minimized = true;
            }
            else if (std::holds_alternative<
                         platform::WindowRestoredEvent>(event)) {
                observed_restored = true;
            }
            else if (std::holds_alternative<
                    platform::WindowCloseRequestedEvent>(event)) {
                observed_close_request = true;
                accept_close = true;
            }
            else if (std::holds_alternative<
                         platform::WindowClosedEvent>(event)) {
                observed_closed = true;
            }
        }

        const auto dropped_events = application.dropped_event_count();
        if (dropped_events != 0) {
            core::log_message(
                core::LogLevel::warning,
                "window",
                std::string{"Dropped platform events: "} +
                    std::to_string(dropped_events));
        }
        application.clear_events();

        if (accept_close) {
            if (asynchronous_close.joinable()) {
                asynchronous_close.join();
            }
            if (asynchronous_close_error.has_value()) {
                return core::Result<void>::failure(
                    std::move(*asynchronous_close_error));
            }
            auto close_result = application.close_window();
            if (!close_result) {
                return core::Result<void>::failure(
                    std::move(close_result).error());
            }
            continue;
        }

        const auto pump = std::move(pump_result).value();
        if (pump.quit_requested || !application.running()) {
            break;
        }

        entered_idle_wait = true;
        if (smoke_mode && !asynchronous_close.joinable()) {
            asynchronous_close = std::jthread(
                [&application, &asynchronous_close_error] {
                    auto close_result = application.request_close();
                    if (!close_result) {
                        asynchronous_close_error.emplace(
                            std::move(close_result).error());
                    }
                });
        }
        auto wait_result = application.wait_for_events();
        if (!wait_result) {
            return core::Result<void>::failure(
                std::move(wait_result).error());
        }
    }

    if (asynchronous_close.joinable()) {
        asynchronous_close.join();
    }
    if (asynchronous_close_error.has_value()) {
        return core::Result<void>::failure(
            std::move(*asynchronous_close_error));
    }

    if (smoke_mode &&
        (!observed_resize ||
         !observed_minimized ||
         !observed_restored ||
         !observed_close_request ||
         !observed_closed ||
         !entered_idle_wait)) {
        return core::Result<void>::failure(core::Error{
            core::ErrorCategory::platform,
            core::ErrorCode::operation_failed,
            "Platform smoke test did not observe the complete native lifecycle",
        });
    }

    return core::Result<void>::success();
}

} // namespace

int main(const int argument_count, char** const arguments)
{
    using namespace shark;

    const auto logging_result = core::initialize_logging();
    if (!logging_result) {
        return EXIT_FAILURE;
    }
    const LoggingSession logging_session;

    const bool smoke_mode =
        argument_count == 2 &&
        std::string_view{arguments[1]} == "--platform-smoke";
    if (argument_count != 1 && !smoke_mode) {
        core::log_message(
            core::LogLevel::error,
            "sandbox",
            "Usage: SharkSandbox [--platform-smoke]");
        return EXIT_FAILURE;
    }

    try {
        auto run_result = run_platform_shell(smoke_mode);
        if (!run_result) {
            core::log_message(
                core::LogLevel::error,
                "platform",
                run_result.error().message());
            return EXIT_FAILURE;
        }
    }
    catch (const std::exception& exception) {
        core::log_message(
            core::LogLevel::critical,
            "sandbox",
            exception.what());
        return EXIT_FAILURE;
    }
    catch (...) {
        core::log_message(
            core::LogLevel::critical,
            "sandbox",
            "Unhandled non-standard exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
