#pragma once

#include <shark/core/result.hpp>
#include <shark/platform/events.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace shark::platform {

struct ApplicationConfig final {
    std::string title{"Shark Sandbox"};
    WindowExtent client_extent{1280, 720};
    bool visible{true};
};

struct MessagePumpResult final {
    bool quit_requested{};
    int exit_code{};
};

struct NativeWindowHandle final {
    void* value{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value != nullptr;
    }
};

class Application final {
public:
    [[nodiscard]] static core::Result<Application> create(
        const ApplicationConfig& config = {});

    Application(Application&&) noexcept;
    Application& operator=(Application&&) = delete;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application();

    [[nodiscard]] core::Result<MessagePumpResult> poll_events();
    [[nodiscard]] core::Result<void> wait_for_events();
    [[nodiscard]] core::Result<void> show_window();
    [[nodiscard]] core::Result<void> resize_client(WindowExtent extent);
    [[nodiscard]] core::Result<void> minimize_window();
    [[nodiscard]] core::Result<void> restore_window();
    [[nodiscard]] core::Result<void> request_close();
    [[nodiscard]] core::Result<void> close_window();

    [[nodiscard]] std::span<const Event> events() const noexcept;
    [[nodiscard]] std::size_t dropped_event_count() const noexcept;
    void clear_events() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool minimized() const noexcept;
    [[nodiscard]] WindowExtent client_extent() const noexcept;
    [[nodiscard]] NativeWindowHandle native_window_handle() const noexcept;

private:
    struct Implementation;

    explicit Application(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;
};

} // namespace shark::platform
