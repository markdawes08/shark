#include <shark/platform/application.hpp>

#include <shark/core/assertion.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace shark::platform {
namespace {

constexpr wchar_t window_class_name[] = L"Shark.Platform.Window.v1";
constexpr std::uint32_t maximum_client_extent = 65'535;
constexpr DWORD shark_window_style = WS_OVERLAPPEDWINDOW;

[[nodiscard]] bool valid_client_extent(
    const WindowExtent extent) noexcept
{
    return extent.width != 0 &&
        extent.height != 0 &&
        extent.width <= maximum_client_extent &&
        extent.height <= maximum_client_extent;
}

[[nodiscard]] core::Error platform_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::platform,
        code,
        std::move(message),
    };
}

[[nodiscard]] DWORD usable_windows_error(const DWORD error) noexcept
{
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

[[nodiscard]] core::Error windows_error(
    const std::string_view operation,
    const DWORD raw_error)
{
    const auto native_error = usable_windows_error(raw_error);
    std::string message{operation};
    message.append(" failed with Win32 error ");
    message.append(std::to_string(native_error));

    std::error_code error_code{
        static_cast<int>(native_error),
        std::system_category(),
    };
    auto native_message = error_code.message();
    while (!native_message.empty() &&
           (native_message.back() == '\r' ||
            native_message.back() == '\n' ||
            native_message.back() == ' ')) {
        native_message.pop_back();
    }
    if (!native_message.empty()) {
        message.append(": ");
        message.append(native_message);
    }

    return platform_error(core::ErrorCode::operation_failed, std::move(message));
}

[[nodiscard]] core::Result<std::wstring> utf8_to_utf16(
    const std::string_view text)
{
    if (text.empty()) {
        return core::Result<std::wstring>::success({});
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return core::Result<std::wstring>::failure(platform_error(
            core::ErrorCode::invalid_argument,
            "Window title is too large to convert from UTF-8"));
    }

    const auto source_size = static_cast<int>(text.size());
    const auto converted_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        source_size,
        nullptr,
        0);
    if (converted_size == 0) {
        auto error = windows_error(
            "Converting the window title from UTF-8",
            GetLastError());
        return core::Result<std::wstring>::failure(core::Error{
            core::ErrorCategory::platform,
            core::ErrorCode::invalid_argument,
            std::move(error).message(),
        });
    }

    std::wstring converted(
        static_cast<std::size_t>(converted_size),
        L'\0');
    const auto written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        source_size,
        converted.data(),
        converted_size);
    if (written != converted_size) {
        return core::Result<std::wstring>::failure(windows_error(
            "Converting the window title from UTF-8",
            GetLastError()));
    }

    return core::Result<std::wstring>::success(std::move(converted));
}

[[nodiscard]] std::int32_t signed_low_word(const LPARAM value) noexcept
{
    const auto bits = static_cast<std::uintptr_t>(value);
    return static_cast<std::int16_t>(bits & 0xffffU);
}

[[nodiscard]] std::int32_t signed_high_word(const LPARAM value) noexcept
{
    const auto bits = static_cast<std::uintptr_t>(value);
    return static_cast<std::int16_t>((bits >> 16U) & 0xffffU);
}

[[nodiscard]] std::int32_t signed_high_word(const WPARAM value) noexcept
{
    return static_cast<std::int16_t>((value >> 16U) & 0xffffU);
}

} // namespace

struct Application::Implementation final {
    static constexpr std::size_t event_capacity = 256;
    static constexpr std::size_t reserved_lifecycle_events = 2;

    HWND window{};
    std::atomic<HWND> close_post_target{};
    DWORD owner_thread{GetCurrentThreadId()};
    WindowExtent extent{};
    bool is_minimized{false};
    bool is_running{true};
    std::array<Event, event_capacity> event_buffer{};
    std::size_t event_count{};
    std::size_t dropped_events{};
    DWORD callback_error{};
    const char* callback_operation{};
    bool close_request_buffered{false};

    ~Implementation() noexcept
    {
        if (window == nullptr) {
            return;
        }

        SHARK_ENSURE(
            GetCurrentThreadId() == owner_thread,
            "Application must be destroyed on its creating thread");
        SHARK_ENSURE(
            DestroyWindow(window) != FALSE,
            "DestroyWindow failed during Application destruction");
    }

    template<typename EventType>
    void push_event(EventType event) noexcept
    {
        static_assert(std::is_nothrow_constructible_v<Event, EventType&&>);
        static_assert(std::is_nothrow_move_assignable_v<Event>);

        constexpr bool is_guaranteed_lifecycle_event =
            std::is_same_v<EventType, WindowCloseRequestedEvent> ||
            std::is_same_v<EventType, WindowClosedEvent>;
        const auto usable_capacity = is_guaranteed_lifecycle_event
            ? event_buffer.size()
            : event_buffer.size() - reserved_lifecycle_events;

        if (event_count < usable_capacity) {
            event_buffer[event_count] = Event{std::move(event)};
            ++event_count;
            return;
        }

        if (dropped_events != std::numeric_limits<std::size_t>::max()) {
            ++dropped_events;
        }
    }

    void record_callback_error(
        const char* const operation,
        const DWORD error) noexcept
    {
        if (callback_error == ERROR_SUCCESS) {
            callback_error = usable_windows_error(error);
            callback_operation = operation;
        }
    }

    [[nodiscard]] static core::Result<void> ensure_window_class(
        const HINSTANCE instance)
    {
        static std::mutex registration_mutex;
        static ATOM registered_class{};
        static HINSTANCE registered_instance{};

        const std::scoped_lock lock{registration_mutex};
        if (registered_class != 0) {
            if (registered_instance != instance) {
                return core::Result<void>::failure(platform_error(
                    core::ErrorCode::invalid_state,
                    "The Shark window class belongs to another module instance"));
            }
            return core::Result<void>::success();
        }

        const auto cursor = LoadCursorW(nullptr, IDC_ARROW);
        if (cursor == nullptr) {
            return core::Result<void>::failure(windows_error(
                "LoadCursorW",
                GetLastError()));
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &Implementation::window_procedure;
        window_class.hInstance = instance;
        window_class.hCursor = cursor;
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        window_class.lpszClassName = window_class_name;

        registered_class = RegisterClassExW(&window_class);
        if (registered_class == 0) {
            return core::Result<void>::failure(windows_error(
                "RegisterClassExW",
                GetLastError()));
        }

        registered_instance = instance;
        return core::Result<void>::success();
    }

    [[nodiscard]] static LRESULT CALLBACK window_procedure(
        const HWND native_window,
        const UINT message,
        const WPARAM word_parameter,
        const LPARAM long_parameter) noexcept
    {
        auto* state = reinterpret_cast<Implementation*>(
            GetWindowLongPtrW(native_window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto* creation =
                reinterpret_cast<const CREATESTRUCTW*>(long_parameter);
            if (creation == nullptr || creation->lpCreateParams == nullptr) {
                return FALSE;
            }

            state = static_cast<Implementation*>(creation->lpCreateParams);
            SetLastError(ERROR_SUCCESS);
            const auto previous = SetWindowLongPtrW(
                native_window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(state));
            const auto error = GetLastError();
            if (previous == 0 && error != ERROR_SUCCESS) {
                state->record_callback_error("SetWindowLongPtrW", error);
                SetLastError(error);
                return FALSE;
            }

            state->window = native_window;
            state->close_post_target.store(
                native_window,
                std::memory_order_release);
            return TRUE;
        }

        if (state == nullptr) {
            return DefWindowProcW(
                native_window,
                message,
                word_parameter,
                long_parameter);
        }

        switch (message) {
        case WM_CLOSE:
            if (!state->close_request_buffered) {
                state->close_request_buffered = true;
                state->push_event(WindowCloseRequestedEvent{});
            }
            return 0;

        case WM_DESTROY:
            state->is_running = false;
            state->push_event(WindowClosedEvent{});
            return 0;

        case WM_NCDESTROY: {
            const auto result = DefWindowProcW(
                native_window,
                message,
                word_parameter,
                long_parameter);
            state->close_post_target.store(nullptr, std::memory_order_release);
            state->window = nullptr;
            static_cast<void>(SetWindowLongPtrW(
                native_window,
                GWLP_USERDATA,
                0));
            return result;
        }

        case WM_SIZE: {
            if (word_parameter == SIZE_MINIMIZED) {
                if (!state->is_minimized) {
                    state->is_minimized = true;
                    state->push_event(WindowMinimizedEvent{});
                }
                return 0;
            }

            if (word_parameter != SIZE_RESTORED &&
                word_parameter != SIZE_MAXIMIZED) {
                break;
            }

            const WindowExtent new_extent{
                static_cast<std::uint32_t>(
                    static_cast<std::uintptr_t>(long_parameter) & 0xffffU),
                static_cast<std::uint32_t>(
                    (static_cast<std::uintptr_t>(long_parameter) >> 16U) &
                    0xffffU),
            };
            const auto was_minimized = state->is_minimized;
            state->is_minimized = false;
            state->extent = new_extent;
            if (was_minimized) {
                state->push_event(WindowRestoredEvent{new_extent});
            }
            state->push_event(WindowResizedEvent{new_extent});
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            const auto bits = static_cast<std::uintptr_t>(long_parameter);
            const auto is_pressed =
                message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            state->push_event(KeyEvent{
                .virtual_key = static_cast<std::uint32_t>(word_parameter),
                .repeat_count = static_cast<std::uint16_t>(bits & 0xffffU),
                .scan_code = static_cast<std::uint16_t>(
                    (bits >> 16U) & 0xffU),
                .action = is_pressed
                    ? KeyAction::pressed
                    : KeyAction::released,
                .extended = (bits & (1ULL << 24U)) != 0,
                .repeated = is_pressed && (bits & (1ULL << 30U)) != 0,
                .system_key =
                    message == WM_SYSKEYDOWN || message == WM_SYSKEYUP,
            });

            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
                return DefWindowProcW(
                    native_window,
                    message,
                    word_parameter,
                    long_parameter);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            state->push_event(MouseMovedEvent{
                .x = signed_low_word(long_parameter),
                .y = signed_high_word(long_parameter),
            });
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            MouseButton button = MouseButton::left;
            if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) {
                button = MouseButton::right;
            }
            else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
                button = MouseButton::middle;
            }
            else if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
                const auto x_button =
                    static_cast<std::uint16_t>(word_parameter >> 16U);
                button = x_button == XBUTTON1
                    ? MouseButton::extra_one
                    : MouseButton::extra_two;
            }

            const auto is_pressed =
                message == WM_LBUTTONDOWN ||
                message == WM_RBUTTONDOWN ||
                message == WM_MBUTTONDOWN ||
                message == WM_XBUTTONDOWN;
            state->push_event(MouseButtonEvent{
                .x = signed_low_word(long_parameter),
                .y = signed_high_word(long_parameter),
                .button = button,
                .action = is_pressed
                    ? ButtonAction::pressed
                    : ButtonAction::released,
            });
            return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP
                ? TRUE
                : 0;
        }

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT client_position{
                .x = signed_low_word(long_parameter),
                .y = signed_high_word(long_parameter),
            };
            SetLastError(ERROR_SUCCESS);
            if (ScreenToClient(native_window, &client_position) == FALSE) {
                state->record_callback_error(
                    "ScreenToClient",
                    GetLastError());
                return 0;
            }

            state->push_event(MouseWheelEvent{
                .x = client_position.x,
                .y = client_position.y,
                .delta = signed_high_word(word_parameter),
                .axis = message == WM_MOUSEWHEEL
                    ? MouseWheelAxis::vertical
                    : MouseWheelAxis::horizontal,
            });
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            SetLastError(ERROR_SUCCESS);
            const auto device_context = BeginPaint(native_window, &paint);
            if (device_context == nullptr) {
                state->record_callback_error("BeginPaint", GetLastError());
                return 0;
            }
            if (EndPaint(native_window, &paint) == FALSE) {
                state->record_callback_error("EndPaint", GetLastError());
            }
            return 0;
        }

        default:
            break;
        }

        return DefWindowProcW(
            native_window,
            message,
            word_parameter,
            long_parameter);
    }
};

Application::Application(
    std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation))
{
}

Application::Application(Application&& other) noexcept
    : implementation_(std::move(other.implementation_))
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application must be moved on its creating thread");
    }
}

Application::~Application() = default;

core::Result<Application> Application::create(
    const ApplicationConfig& config)
{
    if (!valid_client_extent(config.client_extent)) {
        return core::Result<Application>::failure(platform_error(
            core::ErrorCode::invalid_argument,
            "Window client extent must be between 1 and 65535 pixels"));
    }

    try {
        auto title_result = utf8_to_utf16(config.title);
        if (!title_result) {
            return core::Result<Application>::failure(
                std::move(title_result).error());
        }
        auto title = std::move(title_result).value();

        const auto instance = GetModuleHandleW(nullptr);
        if (instance == nullptr) {
            return core::Result<Application>::failure(windows_error(
                "GetModuleHandleW",
                GetLastError()));
        }

        auto class_result = Implementation::ensure_window_class(instance);
        if (!class_result) {
            return core::Result<Application>::failure(
                std::move(class_result).error());
        }

        auto implementation = std::make_unique<Implementation>();
        implementation->extent = config.client_extent;

        RECT window_rectangle{
            .left = 0,
            .top = 0,
            .right = static_cast<LONG>(config.client_extent.width),
            .bottom = static_cast<LONG>(config.client_extent.height),
        };
        if (AdjustWindowRectEx(
                &window_rectangle,
                shark_window_style,
                FALSE,
                0) == FALSE) {
            return core::Result<Application>::failure(windows_error(
                "AdjustWindowRectEx",
                GetLastError()));
        }

        SetLastError(ERROR_SUCCESS);
        const auto window = CreateWindowExW(
            0,
            window_class_name,
            title.c_str(),
            shark_window_style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            window_rectangle.right - window_rectangle.left,
            window_rectangle.bottom - window_rectangle.top,
            nullptr,
            nullptr,
            instance,
            implementation.get());
        if (window == nullptr) {
            const auto error = implementation->callback_error != ERROR_SUCCESS
                ? implementation->callback_error
                : GetLastError();
            const auto operation = implementation->callback_operation != nullptr
                ? implementation->callback_operation
                : "CreateWindowExW";
            return core::Result<Application>::failure(windows_error(
                operation,
                error));
        }

        if (config.visible) {
            static_cast<void>(ShowWindow(window, SW_SHOW));
        }

        if (implementation->callback_error != ERROR_SUCCESS) {
            return core::Result<Application>::failure(windows_error(
                implementation->callback_operation,
                implementation->callback_error));
        }

        return core::Result<Application>::success(
            Application{std::move(implementation)});
    }
    catch (const std::exception& exception) {
        std::string message{"Creating the Win32 application failed: "};
        message.append(exception.what());
        return core::Result<Application>::failure(platform_error(
            core::ErrorCode::operation_failed,
            std::move(message)));
    }
    catch (...) {
        return core::Result<Application>::failure(platform_error(
            core::ErrorCode::operation_failed,
            "Creating the Win32 application failed"));
    }
}

core::Result<MessagePumpResult> Application::poll_events()
{
    if (implementation_ == nullptr) {
        return core::Result<MessagePumpResult>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot poll a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<MessagePumpResult>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Application events must be polled on the creating thread"));
    }

    MessagePumpResult result{
        .quit_requested = false,
        .exit_code = 0,
    };
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            result.quit_requested = true;
            result.exit_code = static_cast<int>(message.wParam);
            implementation_->is_running = false;
            break;
        }

        static_cast<void>(TranslateMessage(&message));
        static_cast<void>(DispatchMessageW(&message));
    }

    if (implementation_->callback_error != ERROR_SUCCESS) {
        const auto error = implementation_->callback_error;
        const auto operation = implementation_->callback_operation;
        implementation_->callback_error = ERROR_SUCCESS;
        implementation_->callback_operation = nullptr;
        return core::Result<MessagePumpResult>::failure(windows_error(
            operation != nullptr ? operation : "The Win32 window procedure",
            error));
    }

    return core::Result<MessagePumpResult>::success(result);
}

core::Result<void> Application::wait_for_events()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot wait using a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Application events must be waited for on the creating thread"));
    }
    if (!implementation_->is_running) {
        return core::Result<void>::success();
    }

    SetLastError(ERROR_SUCCESS);
    if (WaitMessage() == FALSE) {
        return core::Result<void>::failure(windows_error(
            "WaitMessage",
            GetLastError()));
    }
    return core::Result<void>::success();
}

core::Result<void> Application::show_window()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot show a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "The window must be shown from its creating thread"));
    }
    if (implementation_->window == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot show a destroyed window"));
    }

    static_cast<void>(ShowWindow(
        implementation_->window,
        SW_SHOWNOACTIVATE));
    if (IsWindowVisible(implementation_->window) == FALSE) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::operation_failed,
            "ShowWindow did not make the window visible"));
    }
    return core::Result<void>::success();
}

core::Result<void> Application::resize_client(const WindowExtent extent)
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot resize a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "The window must be resized from its creating thread"));
    }
    if (implementation_->window == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot resize a destroyed window"));
    }
    if (!valid_client_extent(extent)) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_argument,
            "Window client extent must be between 1 and 65535 pixels"));
    }

    RECT window_rectangle{
        .left = 0,
        .top = 0,
        .right = static_cast<LONG>(extent.width),
        .bottom = static_cast<LONG>(extent.height),
    };
    if (AdjustWindowRectEx(
            &window_rectangle,
            shark_window_style,
            FALSE,
            0) == FALSE) {
        return core::Result<void>::failure(windows_error(
            "AdjustWindowRectEx",
            GetLastError()));
    }

    SetLastError(ERROR_SUCCESS);
    if (SetWindowPos(
            implementation_->window,
            nullptr,
            0,
            0,
            window_rectangle.right - window_rectangle.left,
            window_rectangle.bottom - window_rectangle.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE) {
        return core::Result<void>::failure(windows_error(
            "SetWindowPos",
            GetLastError()));
    }

    RECT actual_client{};
    if (GetClientRect(implementation_->window, &actual_client) == FALSE) {
        return core::Result<void>::failure(windows_error(
            "GetClientRect",
            GetLastError()));
    }
    const WindowExtent actual_extent{
        static_cast<std::uint32_t>(
            actual_client.right - actual_client.left),
        static_cast<std::uint32_t>(
            actual_client.bottom - actual_client.top),
    };
    if (actual_extent != extent) {
        std::string message{"Native client resize requested "};
        message.append(std::to_string(extent.width));
        message.push_back('x');
        message.append(std::to_string(extent.height));
        message.append(" but produced ");
        message.append(std::to_string(actual_extent.width));
        message.push_back('x');
        message.append(std::to_string(actual_extent.height));
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::operation_failed,
            std::move(message)));
    }

    return core::Result<void>::success();
}

core::Result<void> Application::minimize_window()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot minimize a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "The window must be minimized from its creating thread"));
    }
    if (implementation_->window == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot minimize a destroyed window"));
    }

    static_cast<void>(ShowWindow(
        implementation_->window,
        SW_SHOWMINNOACTIVE));
    if (IsIconic(implementation_->window) == FALSE) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::operation_failed,
            "ShowWindow did not minimize the window"));
    }
    return core::Result<void>::success();
}

core::Result<void> Application::restore_window()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot restore a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "The window must be restored from its creating thread"));
    }
    if (implementation_->window == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot restore a destroyed window"));
    }

    static_cast<void>(ShowWindow(
        implementation_->window,
        SW_SHOWNOACTIVATE));
    if (IsIconic(implementation_->window) != FALSE) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::operation_failed,
            "ShowWindow did not restore the window"));
    }
    return core::Result<void>::success();
}

core::Result<void> Application::request_close()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot close a moved-from Application"));
    }
    const auto close_target = implementation_->close_post_target.load(
        std::memory_order_acquire);
    if (close_target == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot request close after the window has been destroyed"));
    }

    SetLastError(ERROR_SUCCESS);
    if (PostMessageW(close_target, WM_CLOSE, 0, 0) == FALSE) {
        const auto close_error = GetLastError();
        static_cast<void>(PostThreadMessageW(
            implementation_->owner_thread,
            WM_QUIT,
            static_cast<WPARAM>(EXIT_FAILURE),
            0));
        return core::Result<void>::failure(windows_error(
            "PostMessageW(WM_CLOSE)",
            close_error));
    }
    return core::Result<void>::success();
}

core::Result<void> Application::close_window()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "Cannot close a moved-from Application"));
    }
    if (GetCurrentThreadId() != implementation_->owner_thread) {
        return core::Result<void>::failure(platform_error(
            core::ErrorCode::invalid_state,
            "The window must be destroyed from its creating thread"));
    }
    if (implementation_->window == nullptr) {
        return core::Result<void>::success();
    }

    SetLastError(ERROR_SUCCESS);
    if (DestroyWindow(implementation_->window) == FALSE) {
        return core::Result<void>::failure(windows_error(
            "DestroyWindow",
            GetLastError()));
    }
    return core::Result<void>::success();
}

std::span<const Event> Application::events() const noexcept
{
    if (implementation_ == nullptr) {
        return {};
    }
    SHARK_ENSURE(
        GetCurrentThreadId() == implementation_->owner_thread,
        "Application events must be read on the creating thread");
    return std::span<const Event>{
        implementation_->event_buffer.data(),
        implementation_->event_count,
    };
}

std::size_t Application::dropped_event_count() const noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application event state must be read on the creating thread");
    }
    return implementation_ != nullptr
        ? implementation_->dropped_events
        : 0;
}

void Application::clear_events() noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application events must be cleared on the creating thread");
        implementation_->event_count = 0;
        implementation_->dropped_events = 0;
        implementation_->close_request_buffered = false;
    }
}

bool Application::running() const noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application state must be read on the creating thread");
    }
    return implementation_ != nullptr && implementation_->is_running;
}

bool Application::minimized() const noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application state must be read on the creating thread");
    }
    return implementation_ != nullptr && implementation_->is_minimized;
}

WindowExtent Application::client_extent() const noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "Application state must be read on the creating thread");
    }
    return implementation_ != nullptr
        ? implementation_->extent
        : WindowExtent{};
}

NativeWindowHandle Application::native_window_handle() const noexcept
{
    if (implementation_ != nullptr) {
        SHARK_ENSURE(
            GetCurrentThreadId() == implementation_->owner_thread,
            "The native window handle must be read on the creating thread");
    }
    return NativeWindowHandle{
        implementation_ != nullptr ? implementation_->window : nullptr,
    };
}

} // namespace shark::platform
