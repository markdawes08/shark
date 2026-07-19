#pragma once

#include "cube_scene_data.hpp"

#include <shark/renderer/renderer.hpp>

#include <directx/d3d12.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace shark::renderer::d3d12::detail {

inline constexpr std::uint32_t water_camera_root_parameter = 0;
inline constexpr std::uint32_t water_surface_root_parameter = 1;
inline constexpr std::uint32_t water_environment_root_parameter = 2;
inline constexpr std::uint32_t water_surface_root_constant_count = 20;
inline constexpr std::uint32_t water_render_vertex_count = 6;
inline constexpr D3D12_PRIMITIVE_TOPOLOGY water_topology =
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
inline constexpr D3D12_COMPARISON_FUNC water_depth_comparison =
    cube_depth_comparison;
inline constexpr D3D12_DEPTH_WRITE_MASK water_depth_write_mask =
    D3D12_DEPTH_WRITE_MASK_ZERO;
inline constexpr float water_visual_period_seconds =
    40.0F * std::numbers::pi_v<float>;

struct WaterSurfaceRootConstants final {
    math::Float3 camera_world_position{};
    float visual_time_seconds{};

    float center_x{};
    float center_z{};
    float semi_axis_x{};
    float semi_axis_z{};

    float x_warp_square_offset{};
    float x_warp_divisor{};
    float z_warp_square_offset{};
    float z_warp_divisor{};

    float waterline_y{};
    float core_depth{};
    float render_half_extent_x{};
    float render_half_extent_z{};

    std::uint32_t environment_lighting_mode{
        static_cast<std::uint32_t>(
            EnvironmentLightingMode::image_based)};
    float environment_max_lod{5.0F};
    float reserved_zero{};
    float reserved_one{};
};

[[nodiscard]] inline bool valid_water_surface_settings(
    const WaterSurfaceSettings& settings) noexcept
{
    const auto fields_are_valid =
        math::is_finite(settings.center) &&
        std::isfinite(settings.semi_axis_x) &&
        std::isfinite(settings.semi_axis_z) &&
        std::isfinite(settings.x_warp_square_offset) &&
        std::isfinite(settings.x_warp_divisor) &&
        std::isfinite(settings.z_warp_square_offset) &&
        std::isfinite(settings.z_warp_divisor) &&
        std::isfinite(settings.core_depth) &&
        std::isfinite(settings.render_half_extent_x) &&
        std::isfinite(settings.render_half_extent_z) &&
        settings.semi_axis_x > 0.0F &&
        settings.semi_axis_z > 0.0F &&
        settings.x_warp_divisor > 0.0F &&
        settings.z_warp_divisor > 0.0F &&
        settings.core_depth > 0.0F &&
        settings.render_half_extent_x >= settings.semi_axis_x &&
        settings.render_half_extent_z >= settings.semi_axis_z;
    if (!fields_are_valid) {
        return false;
    }

    // Mirror the largest shader intermediates over the authoritative quad.
    // Keeping a wide margin below FLT_MAX prevents finite public inputs from
    // producing infinity during the float multiply/add/divide sequence.
    constexpr auto shader_float_budget =
        static_cast<double>(std::numeric_limits<float>::max()) * 0.25;
    const auto extent_x =
        static_cast<double>(settings.render_half_extent_x);
    const auto extent_z =
        static_cast<double>(settings.render_half_extent_z);
    if (std::abs(static_cast<double>(settings.center.x)) + extent_x >
            shader_float_budget ||
        std::abs(static_cast<double>(settings.center.z)) + extent_z >
            shader_float_budget) {
        return false;
    }

    const auto maximum_x_warp_numerator =
        extent_z * extent_z +
        std::abs(static_cast<double>(
            settings.x_warp_square_offset));
    const auto maximum_z_warp_numerator =
        extent_x * extent_x +
        std::abs(static_cast<double>(
            settings.z_warp_square_offset));
    if (maximum_x_warp_numerator > shader_float_budget ||
        maximum_z_warp_numerator > shader_float_budget) {
        return false;
    }

    const auto maximum_warped_x =
        extent_x +
        maximum_x_warp_numerator /
            static_cast<double>(settings.x_warp_divisor);
    const auto maximum_warped_z =
        extent_z +
        maximum_z_warp_numerator /
            static_cast<double>(settings.z_warp_divisor);
    const auto maximum_normalized_component =
        std::sqrt(shader_float_budget * 0.5);
    return std::isfinite(maximum_warped_x) &&
        std::isfinite(maximum_warped_z) &&
        maximum_warped_x <= shader_float_budget &&
        maximum_warped_z <= shader_float_budget &&
        maximum_warped_x /
                static_cast<double>(settings.semi_axis_x) <=
            maximum_normalized_component &&
        maximum_warped_z /
                static_cast<double>(settings.semi_axis_z) <=
            maximum_normalized_component;
}

[[nodiscard]] inline double water_surface_normalized_radius_squared(
    const WaterSurfaceSettings& settings,
    const float x,
    const float z) noexcept
{
    if (!valid_water_surface_settings(settings) ||
        !std::isfinite(x) ||
        !std::isfinite(z)) {
        return std::numeric_limits<double>::infinity();
    }

    const auto offset_x =
        static_cast<double>(x) -
        static_cast<double>(settings.center.x);
    const auto offset_z =
        static_cast<double>(z) -
        static_cast<double>(settings.center.z);
    const auto warped_x =
        offset_x +
        (offset_z * offset_z -
         static_cast<double>(settings.x_warp_square_offset)) /
            static_cast<double>(settings.x_warp_divisor);
    const auto warped_z =
        offset_z +
        (offset_x * offset_x -
         static_cast<double>(settings.z_warp_square_offset)) /
            static_cast<double>(settings.z_warp_divisor);
    const auto normalized_x =
        warped_x / static_cast<double>(settings.semi_axis_x);
    const auto normalized_z =
        warped_z / static_cast<double>(settings.semi_axis_z);
    return normalized_x * normalized_x +
        normalized_z * normalized_z;
}

[[nodiscard]] inline bool water_surface_contains(
    const WaterSurfaceSettings& settings,
    const float x,
    const float z) noexcept
{
    if (!valid_water_surface_settings(settings) ||
        !std::isfinite(x) ||
        !std::isfinite(z)) {
        return false;
    }

    const auto offset_x =
        std::abs(
            static_cast<double>(x) -
            static_cast<double>(settings.center.x));
    const auto offset_z =
        std::abs(
            static_cast<double>(z) -
            static_cast<double>(settings.center.z));
    return offset_x <=
            static_cast<double>(settings.render_half_extent_x) &&
        offset_z <=
            static_cast<double>(settings.render_half_extent_z) &&
        water_surface_normalized_radius_squared(settings, x, z) <=
            1.0;
}

[[nodiscard]] inline WaterSurfaceRootConstants
make_water_surface_root_constants(
    const WaterSurfaceSettings& settings,
    const math::Float3 camera_world_position,
    const float visual_time_seconds,
    const EnvironmentLightingMode environment_lighting_mode) noexcept
{
    return WaterSurfaceRootConstants{
        .camera_world_position = camera_world_position,
        .visual_time_seconds = std::fmod(
            visual_time_seconds,
            water_visual_period_seconds),
        .center_x = settings.center.x,
        .center_z = settings.center.z,
        .semi_axis_x = settings.semi_axis_x,
        .semi_axis_z = settings.semi_axis_z,
        .x_warp_square_offset = settings.x_warp_square_offset,
        .x_warp_divisor = settings.x_warp_divisor,
        .z_warp_square_offset = settings.z_warp_square_offset,
        .z_warp_divisor = settings.z_warp_divisor,
        .waterline_y = settings.center.y,
        .core_depth = settings.core_depth,
        .render_half_extent_x = settings.render_half_extent_x,
        .render_half_extent_z = settings.render_half_extent_z,
        .environment_lighting_mode =
            static_cast<std::uint32_t>(environment_lighting_mode),
        .environment_max_lod = 5.0F,
        .reserved_zero = 0.0F,
        .reserved_one = 0.0F,
    };
}

static_assert(std::is_standard_layout_v<WaterSurfaceRootConstants>);
static_assert(sizeof(WaterSurfaceRootConstants) == sizeof(float) * 20U);
static_assert(
    water_surface_root_constant_count ==
    sizeof(WaterSurfaceRootConstants) / sizeof(std::uint32_t));
static_assert(water_depth_comparison == D3D12_COMPARISON_FUNC_GREATER_EQUAL);

} // namespace shark::renderer::d3d12::detail
