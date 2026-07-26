#include "water_scene_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

namespace {

[[nodiscard]] constexpr shark::renderer::WaterSurfaceSettings
environment_lab_water() noexcept
{
    return shark::renderer::WaterSurfaceSettings{
        .center = {-128.0F, -4.0F, -128.0F},
        .semi_axis_x = 56.0F,
        .semi_axis_z = 48.0F,
        .x_warp_square_offset = 1'152.0F,
        .x_warp_divisor = 512.0F,
        .z_warp_square_offset = 1'568.0F,
        .z_warp_divisor = 1'024.0F,
        .core_depth = 6.5F,
        .render_half_extent_x = 64.0F,
        .render_half_extent_z = 56.0F,
    };
}

[[nodiscard]] constexpr shark::renderer::WaterSurfaceSettings
island_demo_water() noexcept
{
    return shark::renderer::WaterSurfaceSettings{
        .center = {0.0F, -4.0F, 0.0F},
        .semi_axis_x = 210.0F,
        .semi_axis_z = 170.0F,
        .x_warp_square_offset = 0.0F,
        .x_warp_divisor = 4'096.0F,
        .z_warp_square_offset = 0.0F,
        .z_warp_divisor = 4'096.0F,
        .core_depth = 9.0F,
        .render_half_extent_x = 2'048.0F,
        .render_half_extent_z = 2'048.0F,
        .support =
            shark::renderer::WaterSurfaceSupport::
                outside_warped_footprint,
    };
}

} // namespace

TEST_CASE(
    "water root constants preserve the bounded surface and frame inputs",
    "[renderer][d3d12][water][constants][contract]")
{
    using namespace shark::renderer;
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(water_camera_root_parameter == 0);
    STATIC_REQUIRE(water_surface_root_parameter == 1);
    STATIC_REQUIRE(water_environment_root_parameter == 2);
    STATIC_REQUIRE(water_surface_root_constant_count == 20);
    STATIC_REQUIRE(
        std::is_standard_layout_v<WaterSurfaceRootConstants>);
    STATIC_REQUIRE(
        sizeof(WaterSurfaceRootConstants) == sizeof(float) * 20U);
    STATIC_REQUIRE(
        offsetof(
            WaterSurfaceRootConstants,
            camera_world_position) == 0U);
    STATIC_REQUIRE(
        offsetof(
            WaterSurfaceRootConstants,
            visual_time_seconds) == sizeof(float) * 3U);
    STATIC_REQUIRE(
        offsetof(WaterSurfaceRootConstants, center_x) ==
        sizeof(float) * 4U);
    STATIC_REQUIRE(
        offsetof(WaterSurfaceRootConstants, waterline_y) ==
        sizeof(float) * 12U);
    STATIC_REQUIRE(
        offsetof(
            WaterSurfaceRootConstants,
            environment_lighting_mode) == sizeof(float) * 16U);
    STATIC_REQUIRE(
        offsetof(WaterSurfaceRootConstants, support) ==
        sizeof(float) * 18U);

    constexpr auto settings = environment_lab_water();
    constexpr shark::math::Float3 camera{11.0F, 23.0F, -7.0F};
    constexpr float visual_time_seconds = 37.25F;
    const auto constants = make_water_surface_root_constants(
        settings,
        camera,
        visual_time_seconds,
        EnvironmentLightingMode::procedural_daylight);

    REQUIRE(constants.camera_world_position == camera);
    REQUIRE(constants.visual_time_seconds == visual_time_seconds);
    REQUIRE(constants.center_x == settings.center.x);
    REQUIRE(constants.center_z == settings.center.z);
    REQUIRE(constants.semi_axis_x == settings.semi_axis_x);
    REQUIRE(constants.semi_axis_z == settings.semi_axis_z);
    REQUIRE(
        constants.x_warp_square_offset ==
        settings.x_warp_square_offset);
    REQUIRE(constants.x_warp_divisor == settings.x_warp_divisor);
    REQUIRE(
        constants.z_warp_square_offset ==
        settings.z_warp_square_offset);
    REQUIRE(constants.z_warp_divisor == settings.z_warp_divisor);
    REQUIRE(constants.waterline_y == settings.center.y);
    REQUIRE(constants.core_depth == settings.core_depth);
    REQUIRE(
        constants.render_half_extent_x ==
        settings.render_half_extent_x);
    REQUIRE(
        constants.render_half_extent_z ==
        settings.render_half_extent_z);
    REQUIRE(
        constants.environment_lighting_mode ==
        static_cast<std::uint32_t>(
            EnvironmentLightingMode::procedural_daylight));
    REQUIRE(constants.environment_max_lod == 5.0F);
    REQUIRE(
        constants.support ==
        static_cast<std::uint32_t>(
            WaterSurfaceSupport::inside_warped_footprint));
    REQUIRE(constants.reserved_one == 0.0F);

    const auto wrapped_constants =
        make_water_surface_root_constants(
            settings,
            camera,
            std::numeric_limits<float>::max(),
            EnvironmentLightingMode::image_based);
    REQUIRE(std::isfinite(
        wrapped_constants.visual_time_seconds));
    REQUIRE(wrapped_constants.visual_time_seconds >= 0.0F);
    REQUIRE(
        wrapped_constants.visual_time_seconds <
        water_visual_period_seconds);
    const auto exact_period_constants =
        make_water_surface_root_constants(
            settings,
            camera,
            water_visual_period_seconds,
            EnvironmentLightingMode::image_based);
    REQUIRE(exact_period_constants.visual_time_seconds == 0.0F);
}

TEST_CASE(
    "water surface validation rejects every unsafe public setting",
    "[renderer][d3d12][water][validation]")
{
    using namespace shark::renderer::d3d12::detail;

    REQUIRE(valid_water_surface_settings(environment_lab_water()));
    REQUIRE_FALSE(valid_water_surface_settings({}));

    const auto require_invalid = [](const auto& settings) {
        REQUIRE_FALSE(valid_water_surface_settings(settings));
    };
    const auto not_a_number =
        std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();

    auto invalid = environment_lab_water();
    invalid.center.x = not_a_number;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.center.y = infinity;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.center.z = not_a_number;
    require_invalid(invalid);

    const auto finite_field_cases = std::array{
        &shark::renderer::WaterSurfaceSettings::semi_axis_x,
        &shark::renderer::WaterSurfaceSettings::semi_axis_z,
        &shark::renderer::WaterSurfaceSettings::x_warp_square_offset,
        &shark::renderer::WaterSurfaceSettings::x_warp_divisor,
        &shark::renderer::WaterSurfaceSettings::z_warp_square_offset,
        &shark::renderer::WaterSurfaceSettings::z_warp_divisor,
        &shark::renderer::WaterSurfaceSettings::core_depth,
        &shark::renderer::WaterSurfaceSettings::render_half_extent_x,
        &shark::renderer::WaterSurfaceSettings::render_half_extent_z,
    };
    for (const auto field : finite_field_cases) {
        invalid = environment_lab_water();
        invalid.*field = not_a_number;
        require_invalid(invalid);
    }

    const auto positive_field_cases = std::array{
        &shark::renderer::WaterSurfaceSettings::semi_axis_x,
        &shark::renderer::WaterSurfaceSettings::semi_axis_z,
        &shark::renderer::WaterSurfaceSettings::x_warp_divisor,
        &shark::renderer::WaterSurfaceSettings::z_warp_divisor,
        &shark::renderer::WaterSurfaceSettings::core_depth,
    };
    for (const auto field : positive_field_cases) {
        invalid = environment_lab_water();
        invalid.*field = 0.0F;
        require_invalid(invalid);
        invalid = environment_lab_water();
        invalid.*field = -1.0F;
        require_invalid(invalid);
    }

    invalid = environment_lab_water();
    invalid.render_half_extent_x =
        invalid.semi_axis_x - 0.25F;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.render_half_extent_z =
        invalid.semi_axis_z - 0.25F;
    require_invalid(invalid);

    const auto maximum =
        std::numeric_limits<float>::max();
    const auto minimum_positive =
        std::numeric_limits<float>::denorm_min();
    invalid = environment_lab_water();
    invalid.center.x = maximum;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.render_half_extent_x = maximum;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.x_warp_square_offset = maximum;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.x_warp_divisor = minimum_positive;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.semi_axis_x = minimum_positive;
    require_invalid(invalid);
    invalid = environment_lab_water();
    invalid.support =
        static_cast<shark::renderer::WaterSurfaceSupport>(99U);
    require_invalid(invalid);
}

TEST_CASE(
    "water clip domain selects the intended local warped basin",
    "[renderer][d3d12][water][support][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    constexpr auto settings = environment_lab_water();
    const auto center_radius =
        water_surface_normalized_radius_squared(
            settings,
            settings.center.x,
            settings.center.z);
    const auto expected_center_radius =
        std::pow(-2.25 / 56.0, 2.0) +
        std::pow(-1.53125 / 48.0, 2.0);
    REQUIRE(center_radius == Catch::Approx(expected_center_radius));
    REQUIRE(center_radius < 1.0);
    REQUIRE(water_surface_contains(
        settings,
        settings.center.x,
        settings.center.z));

    REQUIRE(std::isinf(water_surface_normalized_radius_squared(
        {},
        settings.center.x,
        settings.center.z)));
    REQUIRE(std::isinf(water_surface_normalized_radius_squared(
        settings,
        std::numeric_limits<float>::infinity(),
        settings.center.z)));
    REQUIRE(std::isinf(water_surface_normalized_radius_squared(
        settings,
        settings.center.x,
        std::numeric_limits<float>::quiet_NaN())));
    REQUIRE_FALSE(water_surface_contains(
        {},
        settings.center.x,
        settings.center.z));

    // The quadratic warp has another mathematical root far outside the
    // terrain and the procedural quad. The explicit clip domain is therefore
    // authoritative and intentionally excludes that remote component.
    constexpr auto remote_world_x = -942.78025F;
    constexpr auto remote_world_z = -774.77623F;
    REQUIRE(
        water_surface_normalized_radius_squared(
            settings,
            remote_world_x,
            remote_world_z) <
        1.0e-9);
    REQUIRE_FALSE(water_surface_contains(
        settings,
        remote_world_x,
        remote_world_z));

    double maximum_inside_offset_x = 0.0;
    double maximum_inside_offset_z = 0.0;
    std::size_t sampled_inside_points = 0;
    constexpr std::size_t direction_count = 1'440;
    constexpr double radial_step = 0.125;
    constexpr double search_radius = 160.0;

    for (std::size_t direction = 0;
         direction < direction_count;
         ++direction) {
        const auto angle =
            2.0 * std::numbers::pi *
            static_cast<double>(direction) /
            static_cast<double>(direction_count);
        const auto direction_x = std::cos(angle);
        const auto direction_z = std::sin(angle);
        for (double radius = 0.0;
             radius <= search_radius;
             radius += radial_step) {
            const auto offset_x = direction_x * radius;
            const auto offset_z = direction_z * radius;
            const auto normalized_radius =
                water_surface_normalized_radius_squared(
                    settings,
                    settings.center.x +
                        static_cast<float>(offset_x),
                    settings.center.z +
                        static_cast<float>(offset_z));
            if (normalized_radius <= 1.0) {
                ++sampled_inside_points;
                maximum_inside_offset_x = std::max(
                    maximum_inside_offset_x,
                    std::abs(offset_x));
                maximum_inside_offset_z = std::max(
                    maximum_inside_offset_z,
                    std::abs(offset_z));
            }
        }
    }

    REQUIRE(sampled_inside_points > direction_count);
    REQUIRE(
        maximum_inside_offset_x <
        static_cast<double>(settings.render_half_extent_x));
    REQUIRE(
        maximum_inside_offset_z <
        static_cast<double>(settings.render_half_extent_z));

    for (double offset_z = -settings.render_half_extent_z;
         offset_z <= settings.render_half_extent_z;
         offset_z += radial_step) {
        REQUIRE(
            water_surface_normalized_radius_squared(
                settings,
                settings.center.x - settings.render_half_extent_x,
                settings.center.z + static_cast<float>(offset_z)) >
            1.0);
        REQUIRE(
            water_surface_normalized_radius_squared(
                settings,
                settings.center.x + settings.render_half_extent_x,
                settings.center.z + static_cast<float>(offset_z)) >
            1.0);
    }
    for (double offset_x = -settings.render_half_extent_x;
         offset_x <= settings.render_half_extent_x;
         offset_x += radial_step) {
        REQUIRE(
            water_surface_normalized_radius_squared(
                settings,
                settings.center.x + static_cast<float>(offset_x),
                settings.center.z - settings.render_half_extent_z) >
            1.0);
        REQUIRE(
            water_surface_normalized_radius_squared(
                settings,
                settings.center.x + static_cast<float>(offset_x),
                settings.center.z + settings.render_half_extent_z) >
            1.0);
    }
}

TEST_CASE(
    "water support can surround one bounded island footprint",
    "[renderer][d3d12][water][support][island][contract]")
{
    using namespace shark::renderer;
    using namespace shark::renderer::d3d12::detail;

    constexpr auto settings = island_demo_water();
    REQUIRE(valid_water_surface_settings(settings));
    REQUIRE_FALSE(water_surface_contains(settings, 0.0F, 0.0F));
    REQUIRE_FALSE(water_surface_contains(settings, 0.0F, 160.0F));
    REQUIRE(water_surface_contains(settings, 0.0F, 176.0F));
    REQUIRE(water_surface_contains(
        settings,
        -settings.render_half_extent_x,
        -settings.render_half_extent_z));
    REQUIRE(water_surface_contains(
        settings,
        settings.render_half_extent_x,
        settings.render_half_extent_z));
    REQUIRE_FALSE(water_surface_contains(
        settings,
        settings.render_half_extent_x + 1.0F,
        0.0F));

    const auto constants = make_water_surface_root_constants(
        settings,
        {},
        0.0F,
        EnvironmentLightingMode::image_based);
    REQUIRE(
        constants.support ==
        static_cast<std::uint32_t>(
            WaterSurfaceSupport::outside_warped_footprint));

    // Sample the authored quad on a fixed regression grid. This is evidence
    // that this locked scenario exposes one interior dry component and no
    // sampled remote polynomial lobe; it is not a general topology proof or a
    // pixel assertion for arbitrary settings.
    constexpr std::size_t sample_columns = 257;
    constexpr std::size_t sample_rows = 257;
    std::vector<std::uint8_t> excluded(
        sample_columns * sample_rows,
        0U);
    const auto sample_index = [](const std::size_t x,
                                 const std::size_t z) {
        return z * sample_columns + x;
    };
    for (std::size_t z = 0; z < sample_rows; ++z) {
        for (std::size_t x = 0; x < sample_columns; ++x) {
            const auto world_x =
                -settings.render_half_extent_x +
                2.0F * settings.render_half_extent_x *
                    static_cast<float>(x) /
                    static_cast<float>(sample_columns - 1U);
            const auto world_z =
                -settings.render_half_extent_z +
                2.0F * settings.render_half_extent_z *
                    static_cast<float>(z) /
                    static_cast<float>(sample_rows - 1U);
            if (water_surface_normalized_radius_squared(
                    settings,
                    world_x,
                    world_z) < 1.0) {
                excluded[sample_index(x, z)] = 1U;
            }
        }
    }

    std::vector<std::uint8_t> visited(excluded.size(), 0U);
    constexpr std::array<std::array<int, 2>, 4> neighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};
    std::size_t excluded_components = 0;
    bool excluded_touches_quad_edge = false;
    for (std::size_t start_z = 0; start_z < sample_rows; ++start_z) {
        for (std::size_t start_x = 0;
             start_x < sample_columns;
             ++start_x) {
            const auto start = sample_index(start_x, start_z);
            if (excluded[start] == 0U || visited[start] != 0U) {
                continue;
            }
            ++excluded_components;
            std::deque<std::array<std::size_t, 2>> pending;
            pending.push_back({start_x, start_z});
            visited[start] = 1U;
            while (!pending.empty()) {
                const auto point = pending.front();
                pending.pop_front();
                excluded_touches_quad_edge =
                    excluded_touches_quad_edge ||
                    point[0] == 0U ||
                    point[1] == 0U ||
                    point[0] + 1U == sample_columns ||
                    point[1] + 1U == sample_rows;
                for (const auto direction : neighbors) {
                    const auto next_x =
                        static_cast<std::int64_t>(point[0]) +
                        direction[0];
                    const auto next_z =
                        static_cast<std::int64_t>(point[1]) +
                        direction[1];
                    if (next_x < 0 ||
                        next_z < 0 ||
                        next_x >=
                            static_cast<std::int64_t>(
                                sample_columns) ||
                        next_z >=
                            static_cast<std::int64_t>(
                                sample_rows)) {
                        continue;
                    }
                    const auto x =
                        static_cast<std::size_t>(next_x);
                    const auto z =
                        static_cast<std::size_t>(next_z);
                    const auto index = sample_index(x, z);
                    if (excluded[index] == 0U ||
                        visited[index] != 0U) {
                        continue;
                    }
                    visited[index] = 1U;
                    pending.push_back({x, z});
                }
            }
        }
    }
    REQUIRE(excluded_components == 1U);
    REQUIRE_FALSE(excluded_touches_quad_edge);
}

TEST_CASE(
    "procedural water draws six vertices with read-only reversed-Z depth",
    "[renderer][d3d12][water][depth][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(water_render_vertex_count == 6);
    STATIC_REQUIRE(
        water_topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    STATIC_REQUIRE(
        water_depth_comparison ==
        D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    STATIC_REQUIRE(
        water_depth_write_mask == D3D12_DEPTH_WRITE_MASK_ZERO);
}
