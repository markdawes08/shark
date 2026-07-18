#include "daylight_scene_data.hpp"

#include <shark/core/math.hpp>
#include <shark/renderer/renderer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace {

[[nodiscard]] float length(
    const shark::math::Float3 value) noexcept
{
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
}

[[nodiscard]] float brightness(
    const shark::math::Float3 value) noexcept
{
    return value.x + value.y + value.z;
}

void require_approximately_equal(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(0.00001F));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(0.00001F));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(0.00001F));
}

[[nodiscard]] shark::renderer::DaylightSettings
reference_daylight() noexcept
{
    return shark::renderer::DaylightSettings{
        .direction_to_sun = {0.6F, 0.8F, 0.0F},
        .sun_disk_outer_cosine = 0.995F,
        .sun_color = {1.0F, 0.8F, 0.6F},
        .sun_disk_inner_cosine = 0.999F,
        .zenith_color = {0.05F, 0.20F, 0.50F},
        .sky_gradient_exponent = 1.0F,
        .horizon_color = {0.50F, 0.70F, 0.90F},
        .ambient_strength = 0.25F,
        .nadir_color = {0.08F, 0.10F, 0.12F},
        .sun_halo_outer_cosine = 0.95F,
        .sky_ambient_color = {0.30F, 0.40F, 0.50F},
        .sun_intensity = 0.75F,
    };
}

} // namespace

TEST_CASE(
    "daylight settings retain the exact six-row shader layout",
    "[renderer][d3d12][gpu][daylight][layout]")
{
    using shark::renderer::DaylightSettings;

    STATIC_REQUIRE(std::is_standard_layout_v<DaylightSettings>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<DaylightSettings>);
    STATIC_REQUIRE(alignof(DaylightSettings) == 16);
    STATIC_REQUIRE(sizeof(DaylightSettings) == 96);

    STATIC_REQUIRE(offsetof(DaylightSettings, direction_to_sun) == 0);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        sun_disk_outer_cosine) == 12);
    STATIC_REQUIRE(offsetof(DaylightSettings, sun_color) == 16);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        sun_disk_inner_cosine) == 28);
    STATIC_REQUIRE(offsetof(DaylightSettings, zenith_color) == 32);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        sky_gradient_exponent) == 44);
    STATIC_REQUIRE(offsetof(DaylightSettings, horizon_color) == 48);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        ambient_strength) == 60);
    STATIC_REQUIRE(offsetof(DaylightSettings, nadir_color) == 64);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        sun_halo_outer_cosine) == 76);
    STATIC_REQUIRE(offsetof(
        DaylightSettings,
        sky_ambient_color) == 80);
    STATIC_REQUIRE(offsetof(DaylightSettings, sun_intensity) == 92);
}

TEST_CASE(
    "default daylight settings define a finite above-horizon sun",
    "[renderer][d3d12][gpu][daylight][contract]")
{
    using namespace shark;
    using renderer::DaylightSettings;
    using namespace renderer::d3d12;

    const DaylightSettings settings;
    REQUIRE(detail::valid_daylight_settings(settings));
    REQUIRE(math::is_finite(settings.direction_to_sun));
    REQUIRE(length(settings.direction_to_sun) ==
        Catch::Approx(1.0F).margin(0.00001F));
    REQUIRE(settings.direction_to_sun.y > 0.0F);
    REQUIRE(settings.sun_halo_outer_cosine <
        settings.sun_disk_outer_cosine);
    REQUIRE(settings.sun_disk_outer_cosine <
        settings.sun_disk_inner_cosine);
    REQUIRE(settings.sun_disk_inner_cosine <= 1.0F);
    REQUIRE(settings.sky_gradient_exponent > 0.0F);
    REQUIRE(settings.ambient_strength >= 0.0F);
    REQUIRE(settings.sun_intensity > 0.0F);

    REQUIRE(math::is_finite(settings.sun_color));
    REQUIRE(math::is_finite(settings.zenith_color));
    REQUIRE(math::is_finite(settings.horizon_color));
    REQUIRE(math::is_finite(settings.nadir_color));
    REQUIRE(math::is_finite(settings.sky_ambient_color));
}

TEST_CASE(
    "daylight validation rejects malformed directions and parameters",
    "[renderer][d3d12][gpu][daylight][validation]")
{
    using namespace shark;
    using renderer::DaylightSettings;
    using namespace renderer::d3d12;

    auto settings = reference_daylight();
    REQUIRE(detail::valid_daylight_settings(settings));

    SECTION("the direction to the sun must be unit length")
    {
        settings.direction_to_sun = {};
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
        settings.direction_to_sun = {1.2F, 0.0F, 0.0F};
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
    }

    SECTION("non-finite colors cannot reach shader constants")
    {
        settings.horizon_color.y =
            std::numeric_limits<float>::quiet_NaN();
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
    }

    SECTION("LDR color channels stay in the unit interval")
    {
        settings.sun_color.x = -0.01F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));

        settings = reference_daylight();
        settings.zenith_color.z = 1.01F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
    }

    SECTION("the halo and disk cosine intervals must be ordered")
    {
        settings.sun_halo_outer_cosine =
            settings.sun_disk_outer_cosine;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));

        settings = reference_daylight();
        settings.sun_disk_inner_cosine =
            settings.sun_disk_outer_cosine;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));

        settings = reference_daylight();
        settings.sun_disk_inner_cosine = 1.01F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
    }

    SECTION("gradient and light strengths cannot be negative")
    {
        settings.sky_gradient_exponent = -1.0F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));

        settings = reference_daylight();
        settings.ambient_strength = -0.1F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));

        settings = reference_daylight();
        settings.sun_intensity = -0.1F;
        REQUIRE_FALSE(detail::valid_daylight_settings(settings));
    }
}

TEST_CASE(
    "procedural daylight sky has deterministic continuous anchors",
    "[renderer][d3d12][gpu][daylight][sky]")
{
    using namespace shark;
    using namespace renderer::d3d12;

    const auto settings = reference_daylight();
    require_approximately_equal(
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{0.0F, 1.0F, 0.0F}),
        settings.zenith_color);
    require_approximately_equal(
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{0.0F, 0.0F, 1.0F}),
        settings.horizon_color);
    require_approximately_equal(
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{0.0F, -1.0F, 0.0F}),
        settings.nadir_color);

    const auto unit_direction =
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{0.0F, 0.0F, 1.0F});
    const auto scaled_direction =
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{0.0F, 0.0F, 7.0F});
    require_approximately_equal(scaled_direction, unit_direction);

    const auto at_sun = detail::evaluate_daylight_sky_linear(
        settings,
        settings.direction_to_sun);
    const auto away_from_sun =
        detail::evaluate_daylight_sky_linear(
            settings,
            math::Float3{
                -settings.direction_to_sun.x,
                -settings.direction_to_sun.y,
                -settings.direction_to_sun.z,
            });
    REQUIRE(brightness(at_sun) > brightness(away_from_sun));
    REQUIRE(math::is_finite(at_sun));
    REQUIRE(math::is_finite(away_from_sun));
}

TEST_CASE(
    "daylight illumination shares the sky sun direction",
    "[renderer][d3d12][gpu][daylight][lighting]")
{
    using namespace shark;
    using namespace renderer::d3d12;

    const auto settings = reference_daylight();
    const auto facing_sun =
        detail::evaluate_daylight_illumination(
            settings,
            settings.direction_to_sun);
    const auto facing_away =
        detail::evaluate_daylight_illumination(
            settings,
            math::Float3{
                -settings.direction_to_sun.x,
                -settings.direction_to_sun.y,
                -settings.direction_to_sun.z,
            });

    constexpr math::Float3 ground_ambient{
        0.14F,
        0.12F,
        0.10F,
    };
    const auto expected_ambient = [&settings, ground_ambient](
        const float normal_y) {
        const auto hemisphere = normal_y * 0.5F + 0.5F;
        return math::Float3{
            (ground_ambient.x * (1.0F - hemisphere) +
                settings.sky_ambient_color.x * hemisphere) *
                settings.ambient_strength,
            (ground_ambient.y * (1.0F - hemisphere) +
                settings.sky_ambient_color.y * hemisphere) *
                settings.ambient_strength,
            (ground_ambient.z * (1.0F - hemisphere) +
                settings.sky_ambient_color.z * hemisphere) *
                settings.ambient_strength,
        };
    };
    const auto expected_facing_away =
        expected_ambient(-settings.direction_to_sun.y);
    const auto expected_facing_sun =
        expected_ambient(settings.direction_to_sun.y);
    require_approximately_equal(
        facing_away,
        expected_facing_away);
    require_approximately_equal(
        facing_sun,
        math::Float3{
            expected_facing_sun.x +
                settings.sun_color.x *
                    (1.25F * settings.sun_intensity),
            expected_facing_sun.y +
                settings.sun_color.y *
                    (1.25F * settings.sun_intensity),
            expected_facing_sun.z +
                settings.sun_color.z *
                    (1.25F * settings.sun_intensity),
        });
    REQUIRE(brightness(facing_sun) > brightness(facing_away));

    const auto scaled_normal =
        detail::evaluate_daylight_illumination(
            settings,
            math::Float3{
                settings.direction_to_sun.x * 3.0F,
                settings.direction_to_sun.y * 3.0F,
                settings.direction_to_sun.z * 3.0F,
            });
    require_approximately_equal(scaled_normal, facing_sun);
}
