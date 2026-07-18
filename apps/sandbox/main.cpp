#include <shark/assets/dds_cubemap.hpp>
#include <shark/assets/environment_lighting.hpp>
#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/core/result.hpp>
#include <shark/platform/application.hpp>
#include <shark/platform/events.hpp>
#include <shark/rhi/d3d12/device.hpp>
#include <shark/renderer/renderer.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/material_palette.hpp>
#include <shark/world/camera.hpp>

#include "camera_controller.hpp"
#include "options.hpp"

#include <cube.pixel.hpp>
#include <cube.vertex.hpp>
#include <skybox.pixel.hpp>
#include <skybox.vertex.hpp>
#include <terrain.pixel.hpp>
#include <terrain.vertex.hpp>
#include <material_sphere.pixel.hpp>
#include <material_sphere.vertex.hpp>
#include <tone_map.pixel.hpp>
#include <tone_map.vertex.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct TerrainGpuVertex final {
    shark::math::Float3 position;
    shark::math::Float3 normal;
};

static_assert(sizeof(TerrainGpuVertex) == sizeof(float) * 6U);
static_assert(offsetof(TerrainGpuVertex, position) == 0);
static_assert(
    offsetof(TerrainGpuVertex, normal) == sizeof(float) * 3U);

template<std::size_t Size>
[[nodiscard]] std::array<
    shark::renderer::TextureSubresourceDataView,
    shark::terrain::material_subresources_per_array>
make_material_subresource_views(
    const std::array<std::byte, Size>& source) noexcept
{
    using namespace shark;
    static_assert(Size == terrain::material_array_source_bytes);

    std::array<
        renderer::TextureSubresourceDataView,
        terrain::material_subresources_per_array> views{};
    for (std::uint32_t layer = 0;
         layer < terrain::material_layer_count;
         ++layer) {
        for (std::uint32_t mip = 0;
             mip < terrain::material_texture_mip_levels;
             ++mip) {
            const auto index =
                static_cast<std::size_t>(layer) *
                    terrain::material_texture_mip_levels +
                mip;
            const auto width = terrain::material_mip_width(mip);
            const auto height = terrain::material_mip_height(mip);
            const auto row_pitch =
                static_cast<std::size_t>(width) *
                terrain::material_texel_bytes;
            const auto slice_pitch =
                row_pitch * static_cast<std::size_t>(height);
            views[index] = {
                .data = source.data() +
                    terrain::material_subresource_offset(layer, mip),
                .data_size = slice_pitch,
                .width = width,
                .height = height,
                .row_pitch = row_pitch,
                .slice_pitch = slice_pitch,
            };
        }
    }
    return views;
}

template<typename Accessor>
[[nodiscard]] shark::core::Result<
    std::vector<shark::renderer::TextureSubresourceDataView>>
make_environment_subresource_views(
    const std::size_t count,
    Accessor accessor,
    const std::string_view label)
{
    using namespace shark;
    std::vector<renderer::TextureSubresourceDataView> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto source = accessor(index);
        if (!source.has_value()) {
            return core::Result<std::vector<
                renderer::TextureSubresourceDataView>>::failure(
                core::Error{
                    core::ErrorCategory::assets,
                    core::ErrorCode::invalid_state,
                    std::string{label} +
                        " lost a generated subresource",
                });
        }
        result.push_back({
            .data = source->pixels.data(),
            .data_size = source->pixels.size(),
            .width = source->width,
            .height = source->height,
            .row_pitch = source->row_pitch,
            .slice_pitch = source->slice_pitch,
        });
    }
    return core::Result<std::vector<
        renderer::TextureSubresourceDataView>>::success(
            std::move(result));
}

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

[[nodiscard]] std::string format_gpu_milliseconds(
    const std::uint64_t ticks,
    const std::uint64_t frequency,
    const std::uint64_t sample_count = 1)
{
    const auto milliseconds =
        static_cast<long double>(ticks) * 1'000.0L /
        static_cast<long double>(frequency) /
        static_cast<long double>(sample_count);
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << milliseconds;
    return output.str();
}

[[nodiscard]] constexpr shark::renderer::RenderExtent
to_render_extent(
    const shark::platform::WindowExtent extent) noexcept
{
    return shark::renderer::RenderExtent{
        extent.width,
        extent.height,
    };
}

[[nodiscard]] shark::core::Error renderer_smoke_error(
    std::string message)
{
    return shark::core::Error{
        shark::core::ErrorCategory::graphics,
        shark::core::ErrorCode::operation_failed,
        std::move(message),
    };
}

[[nodiscard]] constexpr shark::renderer::TerrainMaterialView
next_terrain_material_view(
    const shark::renderer::TerrainMaterialView view) noexcept
{
    using shark::renderer::TerrainMaterialView;
    switch (view) {
    case TerrainMaterialView::shaded:
        return TerrainMaterialView::material_weights;
    case TerrainMaterialView::material_weights:
        return TerrainMaterialView::shading_normal;
    case TerrainMaterialView::shading_normal:
        return TerrainMaterialView::shaded;
    }
    return TerrainMaterialView::shaded;
}

[[nodiscard]] constexpr std::string_view terrain_material_view_name(
    const shark::renderer::TerrainMaterialView view) noexcept
{
    using shark::renderer::TerrainMaterialView;
    switch (view) {
    case TerrainMaterialView::shaded:
        return "shaded";
    case TerrainMaterialView::material_weights:
        return "ground/rock weights";
    case TerrainMaterialView::shading_normal:
        return "mapped world normal";
    }
    return "invalid";
}

[[nodiscard]] constexpr shark::renderer::EnvironmentLightingMode
next_environment_lighting_mode(
    const shark::renderer::EnvironmentLightingMode mode) noexcept
{
    using shark::renderer::EnvironmentLightingMode;
    return mode == EnvironmentLightingMode::image_based
        ? EnvironmentLightingMode::procedural_daylight
        : EnvironmentLightingMode::image_based;
}

[[nodiscard]] constexpr std::string_view
environment_lighting_mode_name(
    const shark::renderer::EnvironmentLightingMode mode) noexcept
{
    using shark::renderer::EnvironmentLightingMode;
    return mode == EnvironmentLightingMode::image_based
        ? "HDR image-based lighting"
        : "procedural daylight fallback";
}

[[nodiscard]] shark::core::Result<std::filesystem::path>
executable_directory()
{
    using namespace shark;

    std::vector<wchar_t> path_buffer(260);
    for (;;) {
        const auto length = GetModuleFileNameW(
            nullptr,
            path_buffer.data(),
            static_cast<DWORD>(path_buffer.size()));
        if (length == 0) {
            return core::Result<std::filesystem::path>::failure(
                core::Error{
                    core::ErrorCategory::platform,
                    core::ErrorCode::operation_failed,
                    "GetModuleFileNameW failed while locating startup "
                    "content",
                });
        }
        if (length < path_buffer.size()) {
            return core::Result<std::filesystem::path>::success(
                std::filesystem::path{
                    path_buffer.data(),
                    path_buffer.data() + length}.parent_path());
        }
        if (path_buffer.size() >= 32'768U) {
            return core::Result<std::filesystem::path>::failure(
                core::Error{
                    core::ErrorCategory::platform,
                    core::ErrorCode::operation_failed,
                    "The Shark executable path exceeds the supported "
                    "Windows path length",
                });
        }
        path_buffer.resize(
            std::min<std::size_t>(
                path_buffer.size() * 2U,
                32'768U));
    }
}

[[nodiscard]] shark::core::Result<shark::assets::DdsCubemap>
load_startup_cubemap()
{
    using namespace shark;

    auto directory_result = executable_directory();
    if (!directory_result) {
        return core::Result<assets::DdsCubemap>::failure(
            std::move(directory_result).error());
    }
    const auto path =
        directory_result.value() /
        "content" /
        "sky" /
        "shark_orientation_sky_srgb.dds";
    return assets::load_dds_cubemap_file(
        path,
        assets::TextureColorSpace::srgb);
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

[[nodiscard]] shark::core::Result<void> shutdown_and_validate_renderer(
    shark::renderer::Renderer& renderer_instance,
    shark::rhi::d3d12::Device& device,
    bool& debug_state_validated)
{
    using namespace shark;

    if (!renderer_instance.is_shutdown()) {
        auto shutdown_result = renderer_instance.shutdown();
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

[[nodiscard]] shark::core::Result<void> run_renderer_shell(
    shark::rhi::d3d12::Device& device,
    const bool smoke_mode)
{
    using namespace shark;

    const auto focused_gpu_validation =
        device.gpu_based_validation_enabled();
    const auto exercise_minimize_restore = !focused_gpu_validation;
    const std::uint64_t required_smoke_frames =
        focused_gpu_validation ? 120 : 1'000;
    const auto resize_after_frames = required_smoke_frames / 4;
    const auto minimize_after_frames = required_smoke_frames / 2;
    const auto change_camera_after_frames =
        required_smoke_frames * 3 / 4;
    constexpr platform::WindowExtent smoke_resize_extent{960, 600};
    const auto smoke_deadline_duration =
        focused_gpu_validation
        ? std::chrono::seconds{180}
        : std::chrono::seconds{75};

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

    auto cubemap_result = load_startup_cubemap();
    if (!cubemap_result) {
        return core::Result<void>::failure(
            std::move(cubemap_result).error());
    }
    auto cubemap = std::move(cubemap_result).value();
    std::vector<renderer::TextureSubresourceDataView>
        cubemap_subresources;
    cubemap_subresources.reserve(cubemap.subresource_count());
    for (std::size_t index = 0;
         index < cubemap.subresource_count();
         ++index) {
        const auto subresource = cubemap.subresource(index);
        if (!subresource.has_value()) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCategory::assets,
                core::ErrorCode::invalid_state,
                "The loaded startup cubemap lost a validated "
                "subresource",
            });
        }
        cubemap_subresources.push_back({
            .data = subresource->pixels.data(),
            .data_size = subresource->pixels.size(),
            .width = subresource->width,
            .height = subresource->height,
            .row_pitch = subresource->row_pitch,
            .slice_pitch = subresource->slice_pitch,
        });
    }
    const auto cubemap_format =
        cubemap.format() == assets::TextureFormat::rgba8_unorm_srgb
        ? renderer::TextureDataFormat::rgba8_unorm_srgb
        : renderer::TextureDataFormat::rgba8_unorm;

    auto environment_result =
        assets::generate_deterministic_environment_lighting();
    if (!environment_result) {
        return core::Result<void>::failure(
            std::move(environment_result).error());
    }
    auto environment = std::move(environment_result).value();
    auto radiance_views_result =
        make_environment_subresource_views(
            environment.radiance_subresource_count(),
            [&environment](const std::size_t index) {
                return environment.radiance_subresource(index);
            },
            "Environment radiance");
    if (!radiance_views_result) {
        return core::Result<void>::failure(
            std::move(radiance_views_result).error());
    }
    auto radiance_views =
        std::move(radiance_views_result).value();
    auto irradiance_views_result =
        make_environment_subresource_views(
            environment.irradiance_subresource_count(),
            [&environment](const std::size_t index) {
                return environment.irradiance_subresource(index);
            },
            "Environment irradiance");
    if (!irradiance_views_result) {
        return core::Result<void>::failure(
            std::move(irradiance_views_result).error());
    }
    auto irradiance_views =
        std::move(irradiance_views_result).value();
    auto specular_views_result =
        make_environment_subresource_views(
            environment.specular_subresource_count(),
            [&environment](const std::size_t index) {
                return environment.specular_subresource(index);
            },
            "Environment prefiltered specular");
    if (!specular_views_result) {
        return core::Result<void>::failure(
            std::move(specular_views_result).error());
    }
    auto specular_views =
        std::move(specular_views_result).value();
    const auto brdf_lut = environment.brdf_lut();
    const std::array<renderer::TextureSubresourceDataView, 1>
        brdf_lut_views{{
            {
                .data = brdf_lut.pixels.data(),
                .data_size = brdf_lut.pixels.size(),
                .width = brdf_lut.width,
                .height = brdf_lut.height,
                .row_pitch = brdf_lut.row_pitch,
                .slice_pitch = brdf_lut.slice_pitch,
            },
        }};

    auto terrain_surface_result = terrain::HeightTileSurface::create(
        terrain::make_deterministic_height_tile());
    if (!terrain_surface_result) {
        return core::Result<void>::failure(
            std::move(terrain_surface_result).error());
    }
    auto terrain_surface = std::move(terrain_surface_result).value();
    const auto& terrain_tile = terrain_surface.tile();
    auto terrain_mesh_result = terrain::build_lod0_mesh(terrain_tile);
    if (!terrain_mesh_result) {
        return core::Result<void>::failure(
            std::move(terrain_mesh_result).error());
    }
    auto terrain_mesh = std::move(terrain_mesh_result).value();
    if (terrain_surface.bounds() != terrain_mesh.bounds) {
        return core::Result<void>::failure(core::Error{
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_state,
            "The canonical terrain query bounds disagree with the LOD0 "
            "render mesh",
        });
    }
    std::vector<TerrainGpuVertex> terrain_vertices;
    terrain_vertices.reserve(terrain_mesh.positions.size());
    for (std::size_t index = 0;
         index < terrain_mesh.positions.size();
         ++index) {
        terrain_vertices.push_back({
            terrain_mesh.positions[index],
            terrain_mesh.normals[index],
        });
    }
    std::array<TerrainGpuVertex, 8> terrain_bounds_vertices{};
    constexpr math::Float3 bounds_diagnostic_color{
        1.0F,
        -1.0F,
        1.0F,
    };
    for (std::size_t index = 0;
         index < terrain_bounds_vertices.size();
         ++index) {
        terrain_bounds_vertices[index] = TerrainGpuVertex{
            terrain_mesh.bounds_lines.positions[index],
            bounds_diagnostic_color,
        };
    }

    constexpr float query_marker_world_x = -5.125F;
    constexpr float query_marker_world_z = -3.25F;
    const auto query_marker_sample =
        terrain_surface.sample_lod0_surface(
            query_marker_world_x,
            query_marker_world_z);
    if (!query_marker_sample.has_value()) {
        return core::Result<void>::failure(core::Error{
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_state,
            "The deterministic terrain query marker is outside the "
            "canonical LOD0 surface",
        });
    }
    constexpr terrain::Ray3 query_marker_ray{
        {query_marker_world_x, 10.0F, query_marker_world_z},
        {0.0F, -1.0F, 0.0F},
    };
    auto query_marker_hit_result =
        terrain_surface.raycast_lod0(query_marker_ray, 20.0F);
    if (!query_marker_hit_result) {
        return core::Result<void>::failure(
            std::move(query_marker_hit_result).error());
    }
    const auto query_marker_hit =
        query_marker_hit_result.value();
    if (!query_marker_hit.has_value()) {
        return core::Result<void>::failure(core::Error{
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_state,
            "The deterministic terrain query-marker ray missed the "
            "canonical LOD0 surface",
        });
    }
    constexpr float query_agreement_tolerance = 0.00001F;
    const auto nearly_equal = [](const float left, const float right) {
        return std::abs(left - right) <= query_agreement_tolerance;
    };
    const auto equal_position = [&nearly_equal](
                                    const math::Float3 left,
                                    const math::Float3 right) {
        return nearly_equal(left.x, right.x) &&
            nearly_equal(left.y, right.y) &&
            nearly_equal(left.z, right.z);
    };
    const auto& marker_sample = *query_marker_sample;
    const auto& marker_hit = *query_marker_hit;
    if (!equal_position(marker_hit.position, marker_sample.position) ||
        !equal_position(marker_hit.normal, marker_sample.normal) ||
        marker_hit.cell_x != marker_sample.cell_x ||
        marker_hit.cell_z != marker_sample.cell_z ||
        marker_hit.triangle != marker_sample.triangle ||
        !equal_position(
            marker_hit.barycentrics,
            marker_sample.barycentrics) ||
        !nearly_equal(
            marker_hit.distance,
            query_marker_ray.origin.y - marker_sample.position.y)) {
        return core::Result<void>::failure(core::Error{
            core::ErrorCategory::simulation,
            core::ErrorCode::invalid_state,
            "The direct terrain sample and downward LOD0 ray disagree "
            "at the query-marker location",
        });
    }

    constexpr math::Float3 query_marker_diagnostic_color{
        0.0F,
        -1.0F,
        1.0F,
    };
    constexpr float query_marker_length = 1.0F;
    constexpr float query_marker_cross_radius = 0.20F;
    const math::Float3 query_marker_tip{
        marker_sample.position.x +
            marker_sample.normal.x * query_marker_length,
        marker_sample.position.y +
            marker_sample.normal.y * query_marker_length,
        marker_sample.position.z +
            marker_sample.normal.z * query_marker_length,
    };
    const std::array<TerrainGpuVertex, 6> terrain_query_marker_vertices{{
        {marker_sample.position, query_marker_diagnostic_color},
        {query_marker_tip, query_marker_diagnostic_color},
        {{
             query_marker_tip.x - query_marker_cross_radius,
             query_marker_tip.y,
             query_marker_tip.z,
         },
         query_marker_diagnostic_color},
        {{
             query_marker_tip.x + query_marker_cross_radius,
             query_marker_tip.y,
             query_marker_tip.z,
         },
         query_marker_diagnostic_color},
        {{
             query_marker_tip.x,
             query_marker_tip.y,
             query_marker_tip.z - query_marker_cross_radius,
         },
         query_marker_diagnostic_color},
        {{
             query_marker_tip.x,
             query_marker_tip.y,
             query_marker_tip.z + query_marker_cross_radius,
         },
         query_marker_diagnostic_color},
    }};
    constexpr std::array<std::uint16_t, 6>
        terrain_query_marker_indices{{0, 1, 2, 3, 4, 5}};

    const auto terrain_material_palette =
        terrain::make_deterministic_material_palette();
    const auto terrain_albedo_subresources =
        make_material_subresource_views(
            terrain_material_palette.albedo);
    const auto terrain_normal_subresources =
        make_material_subresource_views(
            terrain_material_palette.normal);
    const auto terrain_roughness_subresources =
        make_material_subresource_views(
            terrain_material_palette.roughness);
    const auto make_material_upload_view = [](
        const auto& subresources,
        const renderer::TextureDataFormat format) {
        return renderer::Texture2DArrayUploadView{
            .width = terrain::material_texture_width,
            .height = terrain::material_texture_height,
            .array_layers = terrain::material_layer_count,
            .mip_levels = terrain::material_texture_mip_levels,
            .format = format,
            .subresources = subresources.data(),
            .subresource_count = subresources.size(),
        };
    };

    renderer::RendererConfig renderer_config;
    renderer_config.native_window =
        application.native_window_handle().value;
    renderer_config.extent = to_render_extent(
        application.client_extent());
    renderer_config.textured_cube_vertex_shader = {
        shark_cube_vertex_shader,
        sizeof(shark_cube_vertex_shader),
    };
    renderer_config.textured_cube_pixel_shader = {
        shark_cube_pixel_shader,
        sizeof(shark_cube_pixel_shader),
    };
    renderer_config.skybox_vertex_shader = {
        shark_skybox_vertex_shader,
        sizeof(shark_skybox_vertex_shader),
    };
    renderer_config.skybox_pixel_shader = {
        shark_skybox_pixel_shader,
        sizeof(shark_skybox_pixel_shader),
    };
    renderer_config.terrain_vertex_shader = {
        shark_terrain_vertex_shader,
        sizeof(shark_terrain_vertex_shader),
    };
    renderer_config.terrain_pixel_shader = {
        shark_terrain_pixel_shader,
        sizeof(shark_terrain_pixel_shader),
    };
    renderer_config.material_sphere_vertex_shader = {
        shark_material_sphere_vertex_shader,
        sizeof(shark_material_sphere_vertex_shader),
    };
    renderer_config.material_sphere_pixel_shader = {
        shark_material_sphere_pixel_shader,
        sizeof(shark_material_sphere_pixel_shader),
    };
    renderer_config.tone_map_vertex_shader = {
        shark_tone_map_vertex_shader,
        sizeof(shark_tone_map_vertex_shader),
    };
    renderer_config.tone_map_pixel_shader = {
        shark_tone_map_pixel_shader,
        sizeof(shark_tone_map_pixel_shader),
    };
    renderer_config.startup_cubemap = {
        .width = cubemap.width(),
        .height = cubemap.height(),
        .mip_levels = cubemap.mip_levels(),
        .format = cubemap_format,
        .subresources = cubemap_subresources.data(),
        .subresource_count = cubemap_subresources.size(),
    };
    renderer_config.terrain_mesh = {
        .vertices = terrain_vertices.data(),
        .vertex_count = terrain_vertices.size(),
        .vertex_stride = sizeof(TerrainGpuVertex),
        .indices = terrain_mesh.indices.data(),
        .index_count = terrain_mesh.indices.size(),
        .bounds_vertices = terrain_bounds_vertices.data(),
        .bounds_vertex_count = terrain_bounds_vertices.size(),
        .bounds_vertex_stride = sizeof(TerrainGpuVertex),
        .bounds_indices = terrain_mesh.bounds_lines.indices.data(),
        .bounds_index_count = terrain_mesh.bounds_lines.indices.size(),
        .query_marker_vertices = terrain_query_marker_vertices.data(),
        .query_marker_vertex_count =
            terrain_query_marker_vertices.size(),
        .query_marker_vertex_stride = sizeof(TerrainGpuVertex),
        .query_marker_indices = terrain_query_marker_indices.data(),
        .query_marker_index_count =
            terrain_query_marker_indices.size(),
    };
    renderer_config.terrain_materials = {
        .albedo = make_material_upload_view(
            terrain_albedo_subresources,
            renderer::TextureDataFormat::rgba8_unorm_srgb),
        .normal = make_material_upload_view(
            terrain_normal_subresources,
            renderer::TextureDataFormat::rgba8_unorm),
        .roughness = make_material_upload_view(
            terrain_roughness_subresources,
            renderer::TextureDataFormat::rgba8_unorm),
    };
    const auto make_environment_cube_upload = [](
        const std::uint32_t dimension,
        const std::uint32_t mip_levels,
        const auto& views) {
        return renderer::TextureCubeUploadView{
            .width = dimension,
            .height = dimension,
            .mip_levels = mip_levels,
            .format = renderer::TextureDataFormat::rgba32_float,
            .subresources = views.data(),
            .subresource_count = views.size(),
        };
    };
    renderer_config.environment_lighting = {
        .radiance = make_environment_cube_upload(
            assets::environment_radiance_dimension,
            assets::environment_radiance_mip_levels,
            radiance_views),
        .diffuse_irradiance = make_environment_cube_upload(
            assets::environment_irradiance_dimension,
            assets::environment_irradiance_mip_levels,
            irradiance_views),
        .prefiltered_specular = make_environment_cube_upload(
            assets::environment_specular_dimension,
            assets::environment_specular_mip_levels,
            specular_views),
        .brdf_lut = {
            .width = assets::environment_brdf_lut_dimension,
            .height = assets::environment_brdf_lut_dimension,
            .mip_levels = 1,
            .format = renderer::TextureDataFormat::rgba32_float,
            .subresources = brdf_lut_views.data(),
            .subresource_count = brdf_lut_views.size(),
        },
    };
    renderer_config.synchronize_to_vertical_refresh = !smoke_mode;
    auto renderer_result = renderer::Renderer::create(
        device,
        renderer_config);
    if (!renderer_result) {
        const auto validation_result = device.validate_debug_state();
        if (!validation_result) {
            core::log_message(
                core::LogLevel::error,
                "gpu.validation",
                validation_result.error().message());
        }
        return core::Result<void>::failure(
            std::move(renderer_result).error());
    }
    auto renderer_instance = std::move(renderer_result).value();

    core::log_message(
        core::LogLevel::info,
        "assets.cubemap",
        std::string{
            "Loaded retained DDS orientation fixture for upload "
            "validation (procedural sky does not sample it): "} +
            std::to_string(cubemap.width()) + "x" +
            std::to_string(cubemap.height()) + ", faces=6, mips=" +
            std::to_string(cubemap.mip_levels()) +
            ", subresources=" +
            std::to_string(cubemap.subresource_count()) +
            ", color-space=" +
            (cubemap.color_space() == assets::TextureColorSpace::srgb
                ? "sRGB"
                : "linear"));

    core::log_message(
        core::LogLevel::info,
        "terrain",
        std::string{"Built deterministic LOD0 height tile: samples="} +
            std::to_string(terrain_tile.sample_columns) + "x" +
            std::to_string(terrain_tile.sample_rows) +
            ", vertices=" +
            std::to_string(terrain_mesh.positions.size()) +
            ", triangles=" +
            std::to_string(terrain_mesh.indices.size() / 3U) +
            ", bounds=[(" +
            std::to_string(terrain_mesh.bounds.minimum.x) + ", " +
            std::to_string(terrain_mesh.bounds.minimum.y) + ", " +
            std::to_string(terrain_mesh.bounds.minimum.z) + "), (" +
            std::to_string(terrain_mesh.bounds.maximum.x) + ", " +
            std::to_string(terrain_mesh.bounds.maximum.y) + ", " +
            std::to_string(terrain_mesh.bounds.maximum.z) + ")]");

    core::log_message(
        core::LogLevel::info,
        "terrain.query",
        std::string{"Verified LOD0 sample/ray marker: position=("} +
            std::to_string(marker_sample.position.x) + ", " +
            std::to_string(marker_sample.position.y) + ", " +
            std::to_string(marker_sample.position.z) + "), normal=(" +
            std::to_string(marker_sample.normal.x) + ", " +
            std::to_string(marker_sample.normal.y) + ", " +
            std::to_string(marker_sample.normal.z) + "), cell=(" +
            std::to_string(marker_sample.cell_x) + ", " +
            std::to_string(marker_sample.cell_z) + ")");

    core::log_message(
        core::LogLevel::info,
        "terrain.materials",
        std::string{
            "Created deterministic ground/rock material palette: "
            "arrays=3, extent="} +
            std::to_string(terrain::material_texture_width) + "x" +
            std::to_string(terrain::material_texture_height) +
            ", layers=" +
            std::to_string(terrain::material_layer_count) +
            ", mips=" +
            std::to_string(terrain::material_texture_mip_levels) +
            ", subresources=" +
            std::to_string(terrain::material_total_subresources) +
            ", source-bytes=" +
            std::to_string(terrain::material_total_source_bytes));

    const auto& environment_metadata = environment.metadata();
    core::log_message(
        core::LogLevel::info,
        "sky.environment",
        std::string{
            "Generated deterministic HDR environment lighting: "
            "source=64x32, source-peak="} +
            std::to_string(
                environment_metadata.source_peak_radiance) +
            ", derived-subresources=" +
            std::to_string(
                environment_metadata.derived_subresource_count) +
            ", derived-bytes=" +
            std::to_string(
                environment_metadata.derived_byte_count) +
            ", derived-peak=" +
            std::to_string(
                environment_metadata.derived_peak_value));

    core::log_message(
        core::LogLevel::info,
        "sandbox",
        smoke_mode
            ? "Running fixed " +
                std::to_string(required_smoke_frames) +
                "-frame HDR environment presentation smoke test" +
                (focused_gpu_validation
                    ? " with GPU-based validation"
                    : "")
            : "Direct3D 12 terrain, material sphere, HDR environment, "
              "and tone mapping initialized; F1 toggles "
              "solid/wireframe, F2 cycles terrain material views, and "
              "F3 toggles HDR IBL/procedural daylight");

    world::Camera camera;
    camera.transform.position = {0.0F, 4.0F, 10.0F};
    camera.transform.pitch_radians = -0.35F;
    const renderer::DaylightSettings daylight{};
    sandbox::CameraController camera_controller;
    auto terrain_mode = renderer::TerrainRenderMode::solid;
    auto terrain_material_view =
        renderer::TerrainMaterialView::shaded;
    auto environment_lighting_mode =
        renderer::EnvironmentLightingMode::image_based;
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
    renderer::RendererStats stats_when_minimized{};

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
            if (!smoke_mode) {
                const auto* const key =
                    std::get_if<platform::KeyEvent>(&event);
                if (key != nullptr &&
                    key->virtual_key == VK_F1 &&
                    key->action == platform::KeyAction::pressed &&
                    !key->repeated) {
                    terrain_mode =
                        terrain_mode ==
                            renderer::TerrainRenderMode::solid
                        ? renderer::TerrainRenderMode::wireframe
                        : renderer::TerrainRenderMode::solid;
                    core::log_message(
                        core::LogLevel::info,
                        "terrain",
                        terrain_mode ==
                                renderer::TerrainRenderMode::solid
                            ? "Terrain fill mode: solid"
                            : "Terrain mode: wireframe");
                }
                if (key != nullptr &&
                    key->virtual_key == VK_F2 &&
                    key->action == platform::KeyAction::pressed &&
                    !key->repeated) {
                    terrain_material_view =
                        next_terrain_material_view(
                            terrain_material_view);
                    core::log_message(
                        core::LogLevel::info,
                        "terrain.materials",
                        std::string{"Terrain material view: "} +
                            std::string{terrain_material_view_name(
                                terrain_material_view)});
                }
                if (key != nullptr &&
                    key->virtual_key == VK_F3 &&
                    key->action == platform::KeyAction::pressed &&
                    !key->repeated) {
                    environment_lighting_mode =
                        next_environment_lighting_mode(
                            environment_lighting_mode);
                    core::log_message(
                        core::LogLevel::info,
                        "sky.environment",
                        std::string{"Environment mode: "} +
                            std::string{
                                environment_lighting_mode_name(
                                    environment_lighting_mode)});
                }
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
            return core::Result<void>::failure(renderer_smoke_error(
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
                    renderer_smoke_error(
                            "The presentation smoke window closed before " +
                            std::to_string(required_smoke_frames) +
                            " frames completed"));
            }

            auto shutdown_result = shutdown_and_validate_renderer(
                renderer_instance,
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
                    renderer_smoke_error(
                        "The presentation smoke window did not close "
                        "before its deadline"));
            }
            std::this_thread::yield();
            continue;
        }

        if (pending_resize.has_value() && !application.minimized()) {
            const auto expected_extent = to_render_extent(
                *pending_resize);
            auto resize_result = renderer_instance.resize(expected_extent);
            if (!resize_result) {
                return core::Result<void>::failure(
                    std::move(resize_result).error());
            }
            if (renderer_instance.extent() != expected_extent) {
                return core::Result<void>::failure(
                    renderer_smoke_error(
                        "The swap-chain extent diverged from the physical "
                        "window client extent"));
            }
        }

        if (smoke_mode) {
            if (std::chrono::steady_clock::now() >= smoke_deadline) {
                return core::Result<void>::failure(
                    renderer_smoke_error(
                        "The presentation smoke deadline expired after " +
                        std::to_string(
                            renderer_instance.stats().presented_frames) +
                        " successful frame(s)"));
            }

            const auto presented_frames =
                renderer_instance.stats().presented_frames;
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
                exercise_minimize_restore &&
                !minimize_requested &&
                presented_frames >= minimize_after_frames) {
                stats_when_minimized = renderer_instance.stats();
                auto minimize_result = application.minimize_window();
                if (!minimize_result) {
                    return core::Result<void>::failure(
                        std::move(minimize_result).error());
                }
                minimize_requested = true;
                continue;
            }

            if (application.minimized()) {
                if (minimize_requested && !restore_requested) {
                    observed_minimized_iteration = true;
                    if (renderer_instance.stats() != stats_when_minimized) {
                        return core::Result<void>::failure(
                            renderer_smoke_error(
                                "Render work advanced during the intended "
                                "minimize checkpoint"));
                    }
                }
                if (!smoke_close_posted) {
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
                    (exercise_minimize_restore &&
                        (!observed_minimized ||
                         !observed_minimized_iteration ||
                         !observed_restored ||
                         !observed_restore_resize)) ||
                    !smoke_camera_pose_changed) {
                    return core::Result<void>::failure(
                        renderer_smoke_error(
                            "The presentation smoke lifecycle was incomplete"));
                }

                auto shutdown_result = shutdown_and_validate_renderer(
                    renderer_instance,
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
                 renderer_instance.stats().presented_frames >=
                     change_camera_after_frames) {
            world::advance_camera(
                camera,
                world::CameraMotion{.yaw_radians = 0.25F},
                0.0F,
                1.0F);
            smoke_camera_pose_changed = true;
        }
        if (smoke_mode) {
            const auto presented_frames =
                renderer_instance.stats().presented_frames;
            terrain_mode =
                presented_frames >=
                    required_smoke_frames / 2U
                ? renderer::TerrainRenderMode::wireframe
                : renderer::TerrainRenderMode::solid;
            if (presented_frames <
                required_smoke_frames / 3U) {
                terrain_material_view =
                    renderer::TerrainMaterialView::shaded;
            }
            else if (presented_frames <
                     required_smoke_frames * 2U / 3U) {
                terrain_material_view =
                    renderer::TerrainMaterialView::material_weights;
            }
            else {
                terrain_material_view =
                    renderer::TerrainMaterialView::shading_normal;
            }
            environment_lighting_mode =
                presented_frames <
                    required_smoke_frames * 3U / 4U
                ? renderer::EnvironmentLightingMode::image_based
                : renderer::EnvironmentLightingMode::
                    procedural_daylight;
        }

        const auto render_extent = renderer_instance.extent();
        const auto aspect_ratio =
            static_cast<float>(render_extent.width) /
            static_cast<float>(render_extent.height);
        auto matrices_result = world::build_camera_matrices(
            camera,
            aspect_ratio);
        if (!matrices_result) {
            return core::Result<void>::failure(
                std::move(matrices_result).error());
        }
        const renderer::RenderFrameData frame_data{
            .view_projection =
                matrices_result.value().view_projection,
            .sky_view_projection =
                matrices_result.value().sky_view_projection,
            .daylight = daylight,
            .camera_world_position = camera.transform.position,
            .terrain_mode = terrain_mode,
            .terrain_material_view = terrain_material_view,
            .environment_lighting_mode =
                environment_lighting_mode,
        };
        auto render_result = renderer_instance.render_frame(frame_data);
        if (!render_result) {
            return core::Result<void>::failure(
                std::move(render_result).error());
        }
        if (render_result.value() ==
            renderer::RenderStatus::occluded) {
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
        }
    }

    auto shutdown_result = shutdown_and_validate_renderer(
        renderer_instance,
        device,
        debug_state_validated);
    if (!shutdown_result) {
        return core::Result<void>::failure(
            std::move(shutdown_result).error());
    }

    if (smoke_mode) {
        const auto& stats = renderer_instance.stats();
        if (stats.presented_frames != required_smoke_frames ||
            !observed_close_request ||
            !observed_closed) {
            return core::Result<void>::failure(
                renderer_smoke_error(
                    "The presentation smoke did not finish its close "
                    "lifecycle"));
        }

        constexpr std::uint32_t expected_context_count = 3;
        constexpr std::uint32_t expected_context_mask =
            (std::uint32_t{1} << expected_context_count) - 1;
        constexpr std::uint64_t frame_probe_bytes = 256;
        constexpr std::uint64_t timestamp_queries_per_frame = 10;
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
                stats.frame_submissions * 15 ||
            stats.render_graph_pass_executions !=
                stats.frame_submissions * 4 ||
            stats.render_graph_dependencies !=
                stats.frame_submissions * 3 ||
            stats.render_graph_transition_barriers !=
                stats.frame_submissions * 6 ||
            stats.render_graph_elided_transitions !=
                stats.frame_submissions * 31 ||
            stats.pix_static_upload_events !=
                stats.static_upload_submissions ||
            stats.pix_frame_events != stats.frame_submissions ||
            stats.pix_pass_events !=
                stats.render_graph_pass_executions ||
            stats.pix_terrain_events !=
                stats.frame_submissions ||
            stats.pix_textured_cube_events !=
                stats.frame_submissions ||
            stats.pix_skybox_events !=
                stats.frame_submissions ||
            stats.pix_tone_map_events !=
                stats.frame_submissions ||
            stats.gpu_timestamp_frequency_hz == 0 ||
            stats.timestamp_query_capacity !=
                expected_context_count *
                    timestamp_queries_per_frame ||
            stats.timestamp_query_high_water !=
                timestamp_queries_per_frame ||
            stats.timestamp_queries_written !=
                stats.frame_submissions *
                    timestamp_queries_per_frame ||
            stats.timestamp_resolve_batches !=
                stats.frame_submissions ||
            stats.gpu_timing_samples != stats.frame_submissions ||
            stats.gpu_frame_total_ticks <
                stats.gpu_terrain_total_ticks +
                    stats.gpu_textured_cube_total_ticks +
                    stats.gpu_skybox_total_ticks +
                    stats.gpu_tone_map_total_ticks ||
            stats.gpu_frame_last_ticks <
                stats.gpu_terrain_last_ticks +
                    stats.gpu_textured_cube_last_ticks +
                    stats.gpu_skybox_last_ticks +
                    stats.gpu_tone_map_last_ticks ||
            stats.gpu_frame_max_ticks <
                stats.gpu_terrain_max_ticks ||
            stats.gpu_frame_max_ticks <
                stats.gpu_textured_cube_max_ticks ||
            stats.gpu_frame_max_ticks <
                stats.gpu_skybox_max_ticks ||
            stats.gpu_frame_max_ticks <
                stats.gpu_tone_map_max_ticks ||
            stats.gpu_frame_min_ticks >
                stats.gpu_frame_max_ticks ||
            stats.gpu_terrain_min_ticks >
                stats.gpu_terrain_max_ticks ||
            stats.gpu_textured_cube_min_ticks >
                stats.gpu_textured_cube_max_ticks ||
            stats.gpu_skybox_min_ticks >
                stats.gpu_skybox_max_ticks ||
            stats.gpu_tone_map_min_ticks >
                stats.gpu_tone_map_max_ticks ||
            stats.cube_draw_calls != stats.frame_submissions ||
            stats.skybox_draw_calls != stats.frame_submissions ||
            stats.material_sphere_draw_calls !=
                stats.frame_submissions ||
            stats.tone_map_draw_calls != stats.frame_submissions ||
            stats.terrain_draw_calls != stats.frame_submissions ||
            stats.terrain_bounds_draw_calls !=
                stats.frame_submissions ||
            stats.terrain_query_marker_draw_calls !=
                stats.frame_submissions ||
            stats.terrain_solid_draw_calls == 0 ||
            stats.terrain_wireframe_draw_calls == 0 ||
            stats.terrain_solid_draw_calls +
                    stats.terrain_wireframe_draw_calls !=
                stats.terrain_draw_calls ||
            stats.terrain_shaded_draw_calls == 0 ||
            stats.terrain_material_weight_draw_calls == 0 ||
            stats.terrain_shading_normal_draw_calls == 0 ||
            stats.terrain_shaded_draw_calls +
                    stats.terrain_material_weight_draw_calls +
                    stats.terrain_shading_normal_draw_calls !=
                stats.terrain_draw_calls ||
            stats.terrain_draw_calls +
                    stats.cube_draw_calls +
                    stats.skybox_draw_calls +
                    stats.tone_map_draw_calls !=
                stats.render_graph_pass_executions ||
            stats.cube_indices !=
                stats.cube_draw_calls * 36 ||
            stats.skybox_indices !=
                stats.skybox_draw_calls * 36 ||
            stats.terrain_indices !=
                stats.terrain_draw_calls *
                    terrain::deterministic_tile_index_count ||
            stats.terrain_bounds_indices !=
                stats.terrain_bounds_draw_calls * 24 ||
            stats.terrain_query_marker_indices !=
                stats.terrain_query_marker_draw_calls * 6 ||
            stats.material_sphere_indices !=
                stats.material_sphere_draw_calls * 1'584 ||
            stats.terrain_vertex_count !=
                terrain::deterministic_tile_vertex_count ||
            stats.terrain_index_count !=
                terrain::deterministic_tile_index_count ||
            stats.terrain_bounds_vertex_count != 8 ||
            stats.terrain_bounds_index_count != 24 ||
            stats.terrain_query_marker_vertex_count != 6 ||
            stats.terrain_query_marker_index_count != 6 ||
            stats.material_sphere_vertex_count != 266 ||
            stats.material_sphere_index_count != 1'584 ||
            stats.image_based_lighting_frames == 0 ||
            stats.procedural_daylight_frames == 0 ||
            stats.image_based_lighting_frames +
                    stats.procedural_daylight_frames !=
                stats.frame_submissions ||
            stats.camera_constant_updates !=
                stats.frame_submissions ||
            stats.camera_matrix_changes < 3 ||
            stats.skybox_matrix_changes < 3 ||
            stats.depth_clear_count != stats.frame_submissions ||
            stats.depth_resource_creations !=
                stats.resize_count + 1 ||
            stats.depth_read_view_creations !=
                stats.resize_count + 1 ||
            stats.hdr_scene_color_creations !=
                stats.resize_count + 1 ||
            stats.hdr_scene_color_rtv_creations !=
                stats.resize_count + 1 ||
            stats.hdr_scene_color_srv_creations !=
                stats.resize_count + 1 ||
            stats.texture_bindings !=
                stats.frame_submissions * 4 ||
            stats.terrain_material_bindings !=
                stats.frame_submissions ||
            stats.static_upload_submissions != 1 ||
            stats.geometry_buffer_creations != 4 ||
            stats.checker_texture_creations != 1 ||
            stats.cubemap_texture_creations != 1 ||
            stats.texture_srv_creations !=
                9 + stats.resize_count + 1 ||
            stats.cubemap_srv_creations != 1 ||
            stats.cubemap_faces_uploaded != 6 ||
            stats.cubemap_mip_levels != 1 ||
            stats.cubemap_subresources_uploaded != 6 ||
            stats.cubemap_source_bytes_uploaded != 1'536 ||
            stats.terrain_material_texture_array_creations != 3 ||
            stats.terrain_material_srv_creations != 3 ||
            stats.terrain_material_layers !=
                terrain::material_layer_count ||
            stats.terrain_material_mip_levels !=
                terrain::material_texture_mip_levels ||
            stats.terrain_material_subresources_uploaded !=
                terrain::material_total_subresources ||
            stats.terrain_material_source_bytes_uploaded !=
                terrain::material_total_source_bytes ||
            stats.terrain_material_srgb_resources != 1 ||
            stats.environment_texture_creations != 4 ||
            stats.environment_srv_creations != 4 ||
            stats.environment_cubemap_srv_creations != 3 ||
            stats.environment_subresources_uploaded !=
                assets::environment_derived_subresource_count ||
            stats.environment_source_bytes_uploaded !=
                assets::environment_derived_bytes ||
            stats.environment_hdr_resources != 4 ||
            stats.persistent_texture_descriptors != 10 ||
            stats.cubemap_srgb_resources != 1 ||
            stats.full_queue_drains != stats.resize_count + 1 ||
            stats.last_submission_fence == 0) {
            return core::Result<void>::failure(
                renderer_smoke_error(
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
        summary.append(", graph-dependencies/elided=");
        summary.append(std::to_string(stats.render_graph_dependencies));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.render_graph_elided_transitions));
        summary.append(", pix-events(static/frame/pass)=");
        summary.append(std::to_string(
            stats.pix_static_upload_events));
        summary.push_back('/');
        summary.append(std::to_string(stats.pix_frame_events));
        summary.push_back('/');
        summary.append(std::to_string(stats.pix_pass_events));
        summary.append(", timestamp-queries(high/capacity)=");
        summary.append(std::to_string(
            stats.timestamp_query_high_water));
        summary.push_back('/');
        summary.append(std::to_string(stats.timestamp_query_capacity));
        summary.append(", gpu-samples=");
        summary.append(std::to_string(stats.gpu_timing_samples));
        summary.append(", gpu-frame-ms(avg/max)=");
        summary.append(format_gpu_milliseconds(
            stats.gpu_frame_total_ticks,
            stats.gpu_timestamp_frequency_hz,
            stats.gpu_timing_samples));
        summary.push_back('/');
        summary.append(format_gpu_milliseconds(
            stats.gpu_frame_max_ticks,
            stats.gpu_timestamp_frequency_hz));
        summary.append(", gpu-Terrain-ms(avg/max)=");
        summary.append(format_gpu_milliseconds(
            stats.gpu_terrain_total_ticks,
            stats.gpu_timestamp_frequency_hz,
            stats.gpu_timing_samples));
        summary.push_back('/');
        summary.append(format_gpu_milliseconds(
            stats.gpu_terrain_max_ticks,
            stats.gpu_timestamp_frequency_hz));
        summary.append(", gpu-TexturedCube-ms(avg/max)=");
        summary.append(format_gpu_milliseconds(
            stats.gpu_textured_cube_total_ticks,
            stats.gpu_timestamp_frequency_hz,
            stats.gpu_timing_samples));
        summary.push_back('/');
        summary.append(format_gpu_milliseconds(
            stats.gpu_textured_cube_max_ticks,
            stats.gpu_timestamp_frequency_hz));
        summary.append(", gpu-Skybox-ms(avg/max)=");
        summary.append(format_gpu_milliseconds(
            stats.gpu_skybox_total_ticks,
            stats.gpu_timestamp_frequency_hz,
            stats.gpu_timing_samples));
        summary.push_back('/');
        summary.append(format_gpu_milliseconds(
            stats.gpu_skybox_max_ticks,
            stats.gpu_timestamp_frequency_hz));
        summary.append(", gpu-ToneMap-ms(avg/max)=");
        summary.append(format_gpu_milliseconds(
            stats.gpu_tone_map_total_ticks,
            stats.gpu_timestamp_frequency_hz,
            stats.gpu_timing_samples));
        summary.push_back('/');
        summary.append(format_gpu_milliseconds(
            stats.gpu_tone_map_max_ticks,
            stats.gpu_timestamp_frequency_hz));
        summary.append(", terrain(solid/wire/bounds/query-marker)-draws=");
        summary.append(std::to_string(
            stats.terrain_solid_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_wireframe_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_bounds_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_query_marker_draw_calls));
        summary.append(", terrain(shaded/weights/normals)-views=");
        summary.append(std::to_string(
            stats.terrain_shaded_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_material_weight_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_shading_normal_draw_calls));
        summary.append(", cube/sky-draws=");
        summary.append(std::to_string(stats.cube_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(stats.skybox_draw_calls));
        summary.append(", material-sphere/tone-map-draws=");
        summary.append(std::to_string(
            stats.material_sphere_draw_calls));
        summary.push_back('/');
        summary.append(std::to_string(stats.tone_map_draw_calls));
        summary.append(", camera/sky-matrix-changes=");
        summary.append(std::to_string(stats.camera_matrix_changes));
        summary.push_back('/');
        summary.append(std::to_string(stats.skybox_matrix_changes));
        summary.append(", depth-creations=");
        summary.append(std::to_string(stats.depth_resource_creations));
        summary.append(", hdr-scene-creations=");
        summary.append(std::to_string(
            stats.hdr_scene_color_creations));
        summary.append(", cubemap(faces/mips/subresources/bytes)=");
        summary.append(std::to_string(stats.cubemap_faces_uploaded));
        summary.push_back('/');
        summary.append(std::to_string(stats.cubemap_mip_levels));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.cubemap_subresources_uploaded));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.cubemap_source_bytes_uploaded));
        summary.append(", materials(arrays/layers/mips/subresources/bytes)=");
        summary.append(std::to_string(
            stats.terrain_material_texture_array_creations));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_material_layers));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_material_mip_levels));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_material_subresources_uploaded));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.terrain_material_source_bytes_uploaded));
        summary.append(", environment(resources/cube-srvs/subresources/bytes)=");
        summary.append(std::to_string(
            stats.environment_hdr_resources));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.environment_cubemap_srv_creations));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.environment_subresources_uploaded));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.environment_source_bytes_uploaded));
        summary.append(", lighting-frames(ibl/procedural)=");
        summary.append(std::to_string(
            stats.image_based_lighting_frames));
        summary.push_back('/');
        summary.append(std::to_string(
            stats.procedural_daylight_frames));
        summary.append(", texture-table-binds=");
        summary.append(std::to_string(stats.texture_bindings));
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
        auto run_result = run_renderer_shell(device, smoke_mode);
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
