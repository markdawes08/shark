#pragma once

#include <shark/renderer/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace shark::renderer::d3d12::detail {

inline constexpr std::size_t daylight_constant_bytes =
    sizeof(renderer::DaylightSettings);
inline constexpr std::size_t daylight_direction_offset =
    offsetof(renderer::DaylightSettings, direction_to_sun);
inline constexpr std::size_t daylight_sun_color_offset =
    offsetof(renderer::DaylightSettings, sun_color);
inline constexpr std::size_t daylight_zenith_color_offset =
    offsetof(renderer::DaylightSettings, zenith_color);
inline constexpr std::size_t daylight_horizon_color_offset =
    offsetof(renderer::DaylightSettings, horizon_color);
inline constexpr std::size_t daylight_nadir_color_offset =
    offsetof(renderer::DaylightSettings, nadir_color);
inline constexpr std::size_t daylight_sky_ambient_color_offset =
    offsetof(renderer::DaylightSettings, sky_ambient_color);

static_assert(daylight_constant_bytes == 96);
static_assert(daylight_direction_offset == 0);
static_assert(daylight_sun_color_offset == 16);
static_assert(daylight_zenith_color_offset == 32);
static_assert(daylight_horizon_color_offset == 48);
static_assert(daylight_nadir_color_offset == 64);
static_assert(daylight_sky_ambient_color_offset == 80);

[[nodiscard]] inline bool finite_nonnegative(
    const math::Float3 value) noexcept
{
    return math::is_finite(value) &&
        value.x >= 0.0F &&
        value.y >= 0.0F &&
        value.z >= 0.0F;
}

[[nodiscard]] inline bool unit_interval(
    const math::Float3 value) noexcept
{
    return finite_nonnegative(value) &&
        value.x <= 1.0F &&
        value.y <= 1.0F &&
        value.z <= 1.0F;
}

[[nodiscard]] inline float length_squared(
    const math::Float3 value) noexcept
{
    return value.x * value.x +
        value.y * value.y +
        value.z * value.z;
}

[[nodiscard]] inline bool valid_daylight_settings(
    const renderer::DaylightSettings& settings) noexcept
{
    if (!math::is_finite(settings.direction_to_sun) ||
        !unit_interval(settings.sun_color) ||
        !unit_interval(settings.zenith_color) ||
        !unit_interval(settings.horizon_color) ||
        !unit_interval(settings.nadir_color) ||
        !unit_interval(settings.sky_ambient_color)) {
        return false;
    }

    const auto direction_length_squared =
        length_squared(settings.direction_to_sun);
    constexpr float unit_tolerance = 1.0e-3F;
    if (!std::isfinite(direction_length_squared) ||
        std::abs(direction_length_squared - 1.0F) > unit_tolerance) {
        return false;
    }

    return std::isfinite(settings.sun_disk_outer_cosine) &&
        std::isfinite(settings.sun_disk_inner_cosine) &&
        std::isfinite(settings.sun_halo_outer_cosine) &&
        std::isfinite(settings.sky_gradient_exponent) &&
        std::isfinite(settings.ambient_strength) &&
        std::isfinite(settings.sun_intensity) &&
        settings.sun_halo_outer_cosine >= -1.0F &&
        settings.sun_halo_outer_cosine <
            settings.sun_disk_outer_cosine &&
        settings.sun_disk_outer_cosine <
            settings.sun_disk_inner_cosine &&
        settings.sun_disk_inner_cosine <= 1.0F &&
        settings.sky_gradient_exponent > 0.0F &&
        settings.sky_gradient_exponent <= 8.0F &&
        settings.ambient_strength >= 0.0F &&
        settings.ambient_strength <= 8.0F &&
        settings.sun_intensity >= 0.0F &&
        settings.sun_intensity <= 8.0F;
}

[[nodiscard]] inline math::Float3 scale(
    const math::Float3 value,
    const float scalar) noexcept
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

[[nodiscard]] inline math::Float3 add(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] inline math::Float3 lerp(
    const math::Float3 from,
    const math::Float3 to,
    const float amount) noexcept
{
    return add(
        scale(from, 1.0F - amount),
        scale(to, amount));
}

[[nodiscard]] inline float dot(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

[[nodiscard]] inline math::Float3 normalized(
    const math::Float3 value) noexcept
{
    const auto magnitude_squared = length_squared(value);
    if (!std::isfinite(magnitude_squared) ||
        magnitude_squared <= 0.0F) {
        return {};
    }
    return scale(value, 1.0F / std::sqrt(magnitude_squared));
}

[[nodiscard]] inline float smoothstep(
    const float edge0,
    const float edge1,
    const float value) noexcept
{
    const auto amount = std::clamp(
        (value - edge0) / (edge1 - edge0),
        0.0F,
        1.0F);
    return amount * amount * (3.0F - 2.0F * amount);
}

[[nodiscard]] inline math::Float3 saturate(
    const math::Float3 value) noexcept
{
    return {
        std::clamp(value.x, 0.0F, 1.0F),
        std::clamp(value.y, 0.0F, 1.0F),
        std::clamp(value.z, 0.0F, 1.0F),
    };
}

[[nodiscard]] inline math::Float3 evaluate_daylight_sky_linear(
    const renderer::DaylightSettings& settings,
    const math::Float3 direction) noexcept
{
    const auto unit_direction = normalized(direction);
    if (unit_direction == math::Float3{}) {
        return {};
    }

    const auto up = std::max(unit_direction.y, 0.0F);
    const auto down = std::max(-unit_direction.y, 0.0F);
    const auto upper_sky = lerp(
        settings.horizon_color,
        settings.zenith_color,
        std::pow(up, settings.sky_gradient_exponent));
    const auto lower_sky = lerp(
        settings.horizon_color,
        settings.nadir_color,
        std::pow(down, settings.sky_gradient_exponent));
    auto sky = unit_direction.y >= 0.0F
        ? upper_sky
        : lower_sky;

    const auto sun_cosine = dot(
        unit_direction,
        settings.direction_to_sun);
    const auto disk = smoothstep(
        settings.sun_disk_outer_cosine,
        settings.sun_disk_inner_cosine,
        sun_cosine);
    auto halo_axis = std::clamp(
        (sun_cosine - settings.sun_halo_outer_cosine) /
            (1.0F - settings.sun_halo_outer_cosine),
        0.0F,
        1.0F);
    halo_axis *= halo_axis;
    const auto halo = halo_axis * halo_axis;
    sky = add(
        sky,
        scale(
            settings.sun_color,
            0.16F * halo * (1.0F - disk) *
                settings.sun_intensity));
    const auto sun_disk = saturate(scale(
        settings.sun_color,
        settings.sun_intensity));
    return saturate(lerp(sky, sun_disk, disk));
}

[[nodiscard]] inline math::Float3 evaluate_daylight_illumination(
    const renderer::DaylightSettings& settings,
    const math::Float3 normal) noexcept
{
    const auto unit_normal = normalized(normal);
    if (unit_normal == math::Float3{}) {
        return {};
    }

    constexpr math::Float3 ground_ambient{0.14F, 0.12F, 0.10F};
    const auto hemisphere = std::clamp(
        unit_normal.y * 0.5F + 0.5F,
        0.0F,
        1.0F);
    const auto ambient = scale(
        lerp(
            ground_ambient,
            settings.sky_ambient_color,
            hemisphere),
        settings.ambient_strength);
    const auto diffuse = std::max(
        dot(unit_normal, settings.direction_to_sun),
        0.0F);
    const auto sunlight = scale(
        settings.sun_color,
        1.25F * settings.sun_intensity * diffuse);
    return add(ambient, sunlight);
}

} // namespace shark::renderer::d3d12::detail
