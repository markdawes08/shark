#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/core/result.hpp>
#include <shark/platform/application.hpp>
#include <shark/platform/events.hpp>
#include <shark/rhi/d3d12/device.hpp>
#include <shark/rhi/d3d12/presentation.hpp>
#include <shark/world/camera.hpp>

#include "camera_controller.hpp"
#include "options.hpp"

#include <cube.pixel.hpp>
#include <cube.vertex.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

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

[[nodiscard]] constexpr shark::rhi::d3d12::PresentationExtent
to_presentation_extent(
    const shark::platform::WindowExtent extent) noexcept
{
    return shark::rhi::d3d12::PresentationExtent{
        extent.width,
        extent.height,
    };
}

[[nodiscard]] shark::core::Error presentation_smoke_error(
    std::string message)
{
    return shark::core::Error{
        shark::core::ErrorCategory::graphics,
        shark::core::ErrorCode::operation_failed,
        std::move(message),
    };
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
            [](const platform::WindowFocusChangedEvent& focus) {
                core::log_message(
                    core::LogLevel::debug,
                    "window",
                    focus.focused ? "Focused" : "Focus lost");
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

[[nodiscard]] shark::core::Result<void> shutdown_and_validate_presentation(
    shark::rhi::d3d12::Presentation& presentation,
    shark::rhi::d3d12::Device& device,
    bool& debug_state_validated)
{
    using namespace shark;

    if (!presentation.is_shutdown()) {
        auto shutdown_result = presentation.shutdown();
        if (!shutdown_result) {
            return core::Result<void>::failure(
                std::move(shutdown_result).error());
        }
    }

    if (!debug_state_validated) {
        auto validation_result = device.validate_debug_state();
        if (!validation_result) {
            return core::Result<void>::failure(
                std::move(validation_result).error());
        }
        debug_state_validated = true;
    }

    return core::Result<void>::success();
}

[[nodiscard]] shark::core::Result<void> run_presentation_shell(
    shark::rhi::d3d12::Device& device,
    const bool smoke_mode)
{
    using namespace shark;

    constexpr std::uint64_t resize_after_frames = 250;
    constexpr std::uint64_t minimize_after_frames = 500;
    constexpr std::uint64_t change_camera_after_frames = 750;
    constexpr std::uint64_t required_smoke_frames = 1'000;
    constexpr platform::WindowExtent smoke_resize_extent{960, 600};
    const auto smoke_deadline_duration =
        device.gpu_based_validation_enabled()
        ? std::chrono::seconds{150}
        : std::chrono::seconds{45};

    platform::ApplicationConfig application_config;
    application_config.visible = !smoke_mode;
    auto application_result = platform::Application::create(
        application_config);
    if (!application_result) {
        return core::Result<void>::failure(
            std::move(application_result).error());
    }
    auto application = std::move(application_result).value();

    if (smoke_mode) {
        auto show_result = application.show_window();
        if (!show_result) {
            return core::Result<void>::failure(
                std::move(show_result).error());
        }
    }

    rhi::d3d12::PresentationConfig presentation_config;
    presentation_config.native_window =
        application.native_window_handle().value;
    presentation_config.extent = to_presentation_extent(
        application.client_extent());
    presentation_config.vertex_shader = {
        shark_cube_vertex_shader,
        sizeof(shark_cube_vertex_shader),
    };
    presentation_config.pixel_shader = {
        shark_cube_pixel_shader,
        sizeof(shark_cube_pixel_shader),
    };
    presentation_config.synchronize_to_vertical_refresh = !smoke_mode;
    auto presentation_result = rhi::d3d12::Presentation::create(
        device,
        presentation_config);
    if (!presentation_result) {
        const auto validation_result = device.validate_debug_state();
        if (!validation_result) {
            core::log_message(
                core::LogLevel::error,
                "gpu.validation",
                validation_result.error().message());
        }
        return core::Result<void>::failure(
            std::move(presentation_result).error());
    }
    auto presentation = std::move(presentation_result).value();

    core::log_message(
        core::LogLevel::info,
        "sandbox",
        smoke_mode
            ? "Running fixed 1,000-frame textured-cube presentation "
                "smoke test"
            : "Direct3D 12 textured-cube presentation initialized");

    world::Camera camera;
    sandbox::CameraController camera_controller;
    auto previous_frame_time = std::chrono::steady_clock::now();
    const auto smoke_deadline =
        std::chrono::steady_clock::now() + smoke_deadline_duration;
    bool resize_requested = false;
    bool observed_resize = false;
    bool minimize_requested = false;
    bool observed_minimized = false;
    bool observed_minimized_iteration = false;
    bool restore_requested = false;
    bool observed_restored = false;
    bool observed_restore_resize = false;
    bool smoke_close_posted = false;
    bool observed_close_request = false;
    bool observed_closed = false;
    bool smoke_camera_pose_changed = false;
    bool debug_state_validated = false;
    std::uint64_t frames_when_minimized = 0;
    std::uint64_t submissions_when_minimized = 0;
    std::uint64_t graph_executions_when_minimized = 0;
    std::uint64_t cube_draws_when_minimized = 0;
    std::uint64_t camera_updates_when_minimized = 0;
    std::uint64_t depth_clears_when_minimized = 0;

    for (;;) {
        auto pump_result = application.poll_events();
        if (!pump_result) {
            return core::Result<void>::failure(
                std::move(pump_result).error());
        }

        bool accept_close = false;
        std::optional<platform::WindowExtent> pending_resize;
        for (const auto& event : application.events()) {
            if (const auto* const focus = std::get_if<
                    platform::WindowFocusChangedEvent>(&event);
                focus != nullptr) {
                camera_controller.set_focused(focus->focused);
            }
            camera_controller.handle_event(event);
            log_platform_event(event);
            if (const auto* const resized =
                    std::get_if<platform::WindowResizedEvent>(&event);
                resized != nullptr) {
                pending_resize = resized->client_extent;
                if (resize_requested &&
                    resized->client_extent == smoke_resize_extent) {
                    observed_resize = true;
                }
                if (observed_restored) {
                    observed_restore_resize = true;
                }
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
            camera_controller.reset();
        }
        application.clear_events();
        if (smoke_mode && dropped_events != 0) {
            return core::Result<void>::failure(presentation_smoke_error(
                "The presentation loop dropped " +
                std::to_string(dropped_events) +
                " platform event(s)"));
        }
        if (!smoke_mode && dropped_events != 0) {
            core::log_message(
                core::LogLevel::warning,
                "window",
                std::string{"Dropped platform events: "} +
                    std::to_string(dropped_events));
        }

        if (accept_close) {
            if (smoke_mode && !smoke_close_posted) {
                return core::Result<void>::failure(
                    presentation_smoke_error(
                        "The presentation smoke window closed before "
                        "1,000 frames completed"));
            }

            auto shutdown_result = shutdown_and_validate_presentation(
                presentation,
                device,
                debug_state_validated);
            if (!shutdown_result) {
                return core::Result<void>::failure(
                    std::move(shutdown_result).error());
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

        if (smoke_close_posted) {
            if (std::chrono::steady_clock::now() >= smoke_deadline) {
                return core::Result<void>::failure(
                    presentation_smoke_error(
                        "The presentation smoke window did not close "
                        "before its deadline"));
            }
            std::this_thread::yield();
            continue;
        }

        if (pending_resize.has_value() && !application.minimized()) {
            const auto expected_extent = to_presentation_extent(
                *pending_resize);
            auto resize_result = presentation.resize(expected_extent);
            if (!resize_result) {
                return core::Result<void>::failure(
                    std::move(resize_result).error());
            }
            if (presentation.extent() != expected_extent) {
                return core::Result<void>::failure(
                    presentation_smoke_error(
                        "The swap-chain extent diverged from the physical "
                        "window client extent"));
            }
        }

        if (smoke_mode) {
            if (std::chrono::steady_clock::now() >= smoke_deadline) {
                return core::Result<void>::failure(
                    presentation_smoke_error(
                        "The presentation smoke deadline expired after " +
                        std::to_string(
                            presentation.stats().presented_frames) +
                        " successful frame(s)"));
            }

            const auto presented_frames =
                presentation.stats().presented_frames;
            if (!resize_requested &&
                presented_frames >= resize_after_frames) {
                auto resize_result = application.resize_client(
                    smoke_resize_extent);
                if (!resize_result) {
                    return core::Result<void>::failure(
                        std::move(resize_result).error());
                }
                resize_requested = true;
                continue;
            }

            if (observed_resize &&
                !minimize_requested &&
                presented_frames >= minimize_after_frames) {
                const auto& stats = presentation.stats();
                frames_when_minimized = stats.presented_frames;
                submissions_when_minimized = stats.frame_submissions;
                graph_executions_when_minimized =
                    stats.render_graph_executions;
                cube_draws_when_minimized = stats.cube_draw_calls;
                camera_updates_when_minimized =
                    stats.camera_constant_updates;
                depth_clears_when_minimized =
                    stats.depth_clear_count;
                auto minimize_result = application.minimize_window();
                if (!minimize_result) {
                    return core::Result<void>::failure(
                        std::move(minimize_result).error());
                }
                minimize_requested = true;
                continue;
            }

            if (application.minimized()) {
                observed_minimized_iteration = true;
                const auto& stats = presentation.stats();
                if (stats.presented_frames != frames_when_minimized ||
                    stats.frame_submissions !=
                        submissions_when_minimized ||
                    stats.render_graph_executions !=
                        graph_executions_when_minimized ||
                    stats.cube_draw_calls !=
                        cube_draws_when_minimized ||
                    stats.camera_constant_updates !=
                        camera_updates_when_minimized ||
                    stats.depth_clear_count !=
                        depth_clears_when_minimized) {
                    return core::Result<void>::failure(
                        presentation_smoke_error(
                            "Render work advanced while the window was "
                            "minimized"));
                }
                if (observed_minimized && !restore_requested) {
                    auto restore_result = application.restore_window();
                    if (!restore_result) {
                        return core::Result<void>::failure(
                            std::move(restore_result).error());
                    }
                    restore_requested = true;
                }
                continue;
            }

            if (!smoke_close_posted &&
                presented_frames >= required_smoke_frames) {
                if (presented_frames != required_smoke_frames ||
                    !observed_resize ||
                    !observed_minimized ||
                    !observed_minimized_iteration ||
                    !observed_restored ||
                    !observed_restore_resize ||
                    !smoke_camera_pose_changed) {
                    return core::Result<void>::failure(
                        presentation_smoke_error(
                            "The presentation smoke lifecycle was incomplete"));
                }

                auto shutdown_result = shutdown_and_validate_presentation(
                    presentation,
                    device,
                    debug_state_validated);
                if (!shutdown_result) {
                    return core::Result<void>::failure(
                        std::move(shutdown_result).error());
                }
                auto close_result = application.request_close();
                if (!close_result) {
                    return core::Result<void>::failure(
                        std::move(close_result).error());
                }
                smoke_close_posted = true;
                continue;
            }
        }

        if (application.minimized()) {
            auto wait_result = application.wait_for_events();
            if (!wait_result) {
                return core::Result<void>::failure(
                    std::move(wait_result).error());
            }
            continue;
        }

        const auto frame_time = std::chrono::steady_clock::now();
        const auto elapsed_seconds =
            std::chrono::duration<float>(
                frame_time - previous_frame_time).count();
        previous_frame_time = frame_time;
        if (!smoke_mode) {
            camera_controller.update(camera, elapsed_seconds);
        }
        else if (!smoke_camera_pose_changed &&
                 presentation.stats().presented_frames >=
                     change_camera_after_frames) {
            world::advance_camera(
                camera,
                world::CameraMotion{.yaw_radians = 0.25F},
                0.0F,
                1.0F);
            smoke_camera_pose_changed = true;
        }

        const auto presentation_extent = presentation.extent();
        const auto aspect_ratio =
            static_cast<float>(presentation_extent.width) /
            static_cast<float>(presentation_extent.height);
        auto matrices_result = world::build_camera_matrices(
            camera,
            aspect_ratio);
        if (!matrices_result) {
            return core::Result<void>::failure(
                std::move(matrices_result).error());
        }
        const rhi::d3d12::PresentationFrameData frame_data{
            .view_projection =
                matrices_result.value().view_projection,
        };
        auto present_result = presentation.present_frame(frame_data);
        if (!present_result) {
            return core::Result<void>::failure(
                std::move(present_result).error());
        }
        if (present_result.value() ==
            rhi::d3d12::PresentStatus::occluded) {
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
        }
    }

    auto shutdown_result = shutdown_and_validate_presentation(
        presentation,
        device,
        debug_state_validated);
    if (!shutdown_result) {
        return core::Result<void>::failure(
            std::move(shutdown_result).error());
    }

    if (smoke_mode) {
        const auto& stats = presentation.stats();
        if (stats.presented_frames != required_smoke_frames ||
            !observed_close_request ||
            !observed_closed) {
            return core::Result<void>::failure(
                presentation_smoke_error(
                    "The presentation smoke did not finish its close "
                    "lifecycle"));
        }

        constexpr std::uint32_t expected_context_count = 3;
        constexpr std::uint32_t expected_context_mask =
            (std::uint32_t{1} << expected_context_count) - 1;
        constexpr std::uint64_t frame_probe_bytes = 256;
        const auto attempted_presents =
            stats.presented_frames + stats.occluded_frames;
        if (stats.frame_context_count != expected_context_count ||
            stats.used_frame_context_mask != expected_context_mask ||
            stats.frame_context_acquisitions != attempted_presents ||
            stats.frame_submissions != attempted_presents ||
            stats.retired_frame_submissions !=
                stats.frame_submissions ||
            stats.frame_context_reuses + expected_context_count !=
                stats.frame_context_acquisitions ||
            stats.upload_allocations != stats.frame_submissions ||
            stats.upload_bytes_written !=
                stats.upload_allocations * frame_probe_bytes ||
            stats.upload_high_water_bytes != frame_probe_bytes ||
            stats.descriptor_allocations != stats.frame_submissions ||
            stats.descriptor_high_water_count != 1 ||
            stats.render_graph_compilations !=
                stats.frame_submissions ||
            stats.render_graph_executions !=
                stats.frame_submissions ||
            stats.render_graph_resource_imports !=
                stats.frame_submissions * 2 ||
            stats.render_graph_pass_executions !=
                stats.frame_submissions ||
            stats.render_graph_transition_barriers !=
                stats.frame_submissions * 2 ||
            stats.cube_draw_calls != stats.frame_submissions ||
            stats.cube_draw_calls !=
                stats.render_graph_pass_executions ||
            stats.cube_indices !=
                stats.cube_draw_calls * 36 ||
            stats.camera_constant_updates !=
                stats.frame_submissions ||
            stats.camera_matrix_changes < 3 ||
            stats.depth_clear_count != stats.frame_submissions ||
            stats.depth_resource_creations !=
                stats.resize_count + 1 ||
            stats.texture_bindings != stats.frame_submissions ||
            stats.static_upload_submissions != 1 ||
            stats.geometry_buffer_creations != 2 ||
            stats.checker_texture_creations != 1 ||
            stats.texture_srv_creations != 1 ||
            stats.full_queue_drains != stats.resize_count + 1 ||
            stats.last_submission_fence == 0) {
            return core::Result<void>::failure(
                presentation_smoke_error(
                    "The presentation frame-resource lifecycle invariants "
                    "were not satisfied"));
        }

        auto summary = std::string{"Presentation smoke passed: frames="};
        summary.append(std::to_string(stats.presented_frames));
        summary.append(", occluded=");
        summary.append(std::to_string(stats.occluded_frames));
        summary.append(", resizes=");
        summary.append(std::to_string(stats.resize_count));
        summary.append(", context-reuses=");
        summary.append(std::to_string(stats.frame_context_reuses));
        summary.append(", reuse-waits=");
        summary.append(std::to_string(stats.blocking_reuse_waits));
        summary.append(", queue-drains=");
        summary.append(std::to_string(stats.full_queue_drains));
        summary.append(", upload-high-water=");
        summary.append(std::to_string(stats.upload_high_water_bytes));
        summary.append(", descriptor-high-water=");
        summary.append(std::to_string(
            stats.descriptor_high_water_count));
        summary.append(", graph-passes=");
        summary.append(std::to_string(
            stats.render_graph_pass_executions));
        summary.append(", graph-barriers=");
        summary.append(std::to_string(
            stats.render_graph_transition_barriers));
        summary.append(", cube-draws=");
        summary.append(std::to_string(stats.cube_draw_calls));
        summary.append(", camera-matrix-changes=");
        summary.append(std::to_string(stats.camera_matrix_changes));
        summary.append(", depth-creations=");
        summary.append(std::to_string(stats.depth_resource_creations));
        core::log_message(
            core::LogLevel::info,
            "gpu.presentation",
            summary);
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

    std::vector<std::string_view> argument_views;
    argument_views.reserve(
        static_cast<std::size_t>(argument_count > 0
            ? argument_count - 1
            : 0));
    for (int index = 1; index < argument_count; ++index) {
        argument_views.emplace_back(arguments[index]);
    }
    auto options_result = sandbox::parse_options(argument_views);
    if (!options_result) {
        core::log_message(
            core::LogLevel::error,
            "sandbox",
            options_result.error().message());
        core::log_message(
            core::LogLevel::error,
            "sandbox",
            sandbox::usage());
        return EXIT_FAILURE;
    }
    const auto options = std::move(options_result).value();

    try {
        if (options.run_mode == sandbox::RunMode::platform_smoke) {
            auto run_result = run_platform_shell(true);
            if (!run_result) {
                core::log_message(
                    core::LogLevel::error,
                    "platform",
                    run_result.error().message());
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }

        rhi::d3d12::DeviceConfig device_config;
        device_config.adapter = options.adapter;
        device_config.enable_gpu_based_validation =
            options.gpu_based_validation;
        auto device_result = rhi::d3d12::Device::create(device_config);
        if (!device_result) {
            core::log_message(
                core::LogLevel::error,
                "gpu",
                device_result.error().message());
            return EXIT_FAILURE;
        }
        auto device = std::move(device_result).value();

        if (options.run_mode == sandbox::RunMode::gpu_smoke) {
            core::log_message(
                core::LogLevel::info,
                "sandbox",
                "Direct3D 12 device smoke test passed");
            return EXIT_SUCCESS;
        }
        if (options.run_mode == sandbox::RunMode::capabilities) {
            core::log_message(
                core::LogLevel::info,
                "sandbox",
                "Direct3D 12 capability report completed");
            return EXIT_SUCCESS;
        }

        const auto smoke_mode =
            options.run_mode == sandbox::RunMode::present_smoke;
        auto run_result = run_presentation_shell(device, smoke_mode);
        if (!run_result) {
            core::log_message(
                core::LogLevel::error,
                "gpu.presentation",
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
