#include <shark/fluids/shallow_water_reference.hpp>
#include <shark/fluids/shallow_water_solver.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace {

[[nodiscard]] shark::fluids::ShallowWaterReferenceGrid
make_grid(
    const shark::fluids::ShallowWaterGridConfig config,
    const std::span<const double> bed,
    const std::span<
        const shark::fluids::ShallowWaterConservedState>
        states)
{
    const auto result =
        shark::fluids::make_shallow_water_reference_grid(
            config,
            bed,
            states);
    REQUIRE(result);
    return result.value();
}

[[nodiscard]] shark::fluids::ShallowWaterReferenceGrid
make_flat_grid(
    const std::uint32_t columns,
    const std::uint32_t rows,
    const std::span<const double> depths)
{
    using namespace shark;

    const auto count =
        static_cast<std::size_t>(columns) * rows;
    REQUIRE(depths.size() == count);
    std::array<
        double,
        fluids::shallow_water_reference_cell_capacity>
        bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        fluids::shallow_water_reference_cell_capacity>
        states{};
    for (std::size_t index = 0;
         index < count;
         ++index) {
        states[index].water_depth = depths[index];
    }
    return make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = columns,
            .rows = rows,
            .cell_spacing = 1.0,
        },
        std::span<const double>{bed.data(), count},
        std::span<
            const fluids::ShallowWaterConservedState>{
                states.data(),
                count});
}

struct ActivityScan final {
    std::size_t active{};
    std::size_t retained{};
    std::size_t exact_dry{};
    double retained_volume{};
};

[[nodiscard]] ActivityScan scan_activity(
    const shark::fluids::ShallowWaterReferenceGrid& grid,
    const double threshold)
{
    ActivityScan scan;
    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        const auto& state = grid.states[index];
        if (state.water_depth == 0.0) {
            ++scan.exact_dry;
        } else if (state.water_depth > threshold) {
            ++scan.active;
        } else {
            ++scan.retained;
            scan.retained_volume +=
                state.water_depth * cell_area;
        }
        if (state.water_depth <= threshold) {
            REQUIRE(state.momentum_x == 0.0);
            REQUIRE(state.momentum_z == 0.0);
            REQUIRE_FALSE(std::signbit(state.momentum_x));
            REQUIRE_FALSE(std::signbit(state.momentum_z));
        }
    }
    return scan;
}

void require_closed_ledger(
    const shark::fluids::ShallowWaterAdvanceReport&
        report)
{
    REQUIRE(report.net_outward_boundary_volume == 0.0);
    REQUIRE(std::isfinite(report.volume_balance_tolerance));
    REQUIRE(report.volume_balance_tolerance > 0.0);
    REQUIRE(std::abs(report.volume_balance_residual) <=
        report.volume_balance_tolerance);
    REQUIRE(
        report.maximum_absolute_volume_balance_residual <=
        report.volume_balance_tolerance);
}

void require_report_matches_grid(
    const shark::fluids::ShallowWaterAdvanceReport& report,
    const shark::fluids::ShallowWaterReferenceGrid& grid)
{
    const auto diagnostics =
        shark::fluids::
            inspect_shallow_water_reference_grid(grid);
    REQUIRE(diagnostics);
    const auto scan =
        scan_activity(
            grid,
            report.dry_depth_threshold_meters);

    REQUIRE(
        report.initial_active_cell_count +
            report.initial_retained_film_cell_count +
            report.initial_exact_dry_cell_count ==
        grid.cell_count);
    REQUIRE(report.final_active_cell_count == scan.active);
    REQUIRE(
        report.final_retained_film_cell_count ==
        scan.retained);
    REQUIRE(
        report.final_exact_dry_cell_count ==
        scan.exact_dry);
    REQUIRE(
        scan.active + scan.retained ==
        diagnostics.value().wet_cell_count);
    REQUIRE(scan.exact_dry ==
        diagnostics.value().dry_cell_count);
    REQUIRE(report.final_retained_film_volume ==
        scan.retained_volume);
    REQUIRE(report.final_water_volume ==
        diagnostics.value().water_volume);
    REQUIRE(report.final_minimum_water_depth ==
        diagnostics.value().minimum_water_depth);
    REQUIRE(report.final_maximum_absolute_momentum_x ==
        diagnostics.value().maximum_absolute_momentum_x);
    REQUIRE(report.final_maximum_absolute_momentum_z ==
        diagnostics.value().maximum_absolute_momentum_z);
    REQUIRE(report.advanced_seconds ==
        report.requested_seconds);
    REQUIRE(std::isfinite(
        report.
            cumulative_discarded_absolute_momentum_x));
    REQUIRE(std::isfinite(
        report.
            cumulative_discarded_absolute_momentum_z));
    REQUIRE(
        report.cumulative_discarded_absolute_momentum_x >=
        0.0);
    REQUIRE(
        report.cumulative_discarded_absolute_momentum_z >=
        0.0);
    require_closed_ledger(report);
}

[[nodiscard]] constexpr std::size_t cell_index(
    const std::uint32_t column,
    const std::uint32_t row,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(row) *
        columns + column;
}

} // namespace

TEST_CASE(
    "wet dry threshold classifies depth and projects only momentum",
    "[fluids][shallow-water][wet-dry][threshold]")
{
    using namespace shark;

    constexpr auto threshold = 0.05;
    const std::array depths{
        0.0,
        std::nextafter(threshold, 0.0),
        threshold,
        std::nextafter(
            threshold,
            std::numeric_limits<double>::infinity()),
    };
    for (std::size_t case_index = 0;
         case_index < depths.size();
         ++case_index) {
        const std::array one_depth{depths[case_index]};
        auto grid = make_flat_grid(1, 1, one_depth);
        const auto original = grid;
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.1,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = threshold,
                });

        CAPTURE(case_index, depths[case_index]);
        REQUIRE(report);
        REQUIRE(grid == original);
        const auto expected_exact =
            depths[case_index] == 0.0 ? 1U : 0U;
        const auto expected_active =
            depths[case_index] > threshold ? 1U : 0U;
        const auto expected_retained =
            1U - expected_exact - expected_active;
        REQUIRE(report.value().final_exact_dry_cell_count ==
            expected_exact);
        REQUIRE(report.value().final_active_cell_count ==
            expected_active);
        REQUIRE(
            report.value().final_retained_film_cell_count ==
            expected_retained);
        require_report_matches_grid(report.value(), grid);
    }

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{{
            {threshold, 0.01, -0.02},
        }};
    auto projected = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 2.0,
        },
        bed,
        states);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            projected,
            0.1,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .dry_depth_threshold_meters = threshold,
            });
    REQUIRE(report);
    REQUIRE(projected.states[0].water_depth == threshold);
    REQUIRE(projected.states[0].momentum_x == 0.0);
    REQUIRE(projected.states[0].momentum_z == 0.0);
    REQUIRE(
        report.value().
            cumulative_discarded_absolute_momentum_x ==
        0.04);
    REQUIRE(
        report.value().
            cumulative_discarded_absolute_momentum_z ==
        0.08);
    require_report_matches_grid(report.value(), projected);
}

TEST_CASE(
    "an uneven all dry capacity grid uses one zero rate step",
    "[fluids][shallow-water][wet-dry][dry][capacity][range]")
{
    using namespace shark;

    std::array<
        double,
        fluids::shallow_water_reference_cell_capacity>
        bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        fluids::shallow_water_reference_cell_capacity>
        states{};
    for (std::size_t index = 0;
         index < bed.size();
         ++index) {
        bed[index] =
            index % 2U == 0U ? -1.0E308 : 1.0E308;
    }
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns =
                fluids::shallow_water_reference_max_columns,
            .rows = fluids::shallow_water_reference_max_rows,
            .cell_spacing = 1.0,
        },
        bed,
        states);
    const auto original = grid;
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            10.0);

    REQUIRE(report);
    REQUIRE(grid == original);
    REQUIRE(report.value().substep_count == 1U);
    REQUIRE(report.value().minimum_substep_seconds == 10.0);
    REQUIRE(report.value().maximum_substep_seconds == 10.0);
    REQUIRE(
        report.value().
            maximum_observed_courant_number == 0.0);
    REQUIRE(
        report.value().cumulative_absolute_face_volume ==
        0.0);
    REQUIRE(report.value().final_exact_dry_cell_count ==
        grid.cell_count);
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "zero rate dry steps ignore irrelevant time space ratios",
    "[fluids][shallow-water][wet-dry][dry][range]")
{
    using namespace shark;

    struct RangeCase final {
        double cell_spacing{};
        double requested_seconds{};
    };
    constexpr std::array cases{
        RangeCase{1.0E-161, 1.0E308},
        RangeCase{
            1.0E154,
            std::numeric_limits<double>::denorm_min()},
    };
    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{};

    for (const auto& range_case : cases) {
        auto grid = make_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = range_case.cell_spacing,
            },
            bed,
            states);
        const auto original = grid;
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                range_case.requested_seconds);

        CAPTURE(
            range_case.cell_spacing,
            range_case.requested_seconds);
        REQUIRE(report);
        REQUIRE(grid == original);
        REQUIRE(report.value().substep_count == 1U);
        REQUIRE(
            report.value().minimum_substep_seconds ==
            range_case.requested_seconds);
        REQUIRE(
            report.value().maximum_substep_seconds ==
            range_case.requested_seconds);
        REQUIRE(
            report.value().
                maximum_observed_courant_number == 0.0);
        REQUIRE(
            report.value().cumulative_absolute_face_volume ==
            0.0);
        REQUIRE(
            report.value().final_exact_dry_cell_count == 1U);
        require_report_matches_grid(report.value(), grid);
    }
}

TEST_CASE(
    "a rounded zero CFL rate preserves nonzero wet transport",
    "[fluids][shallow-water][wet-dry][range][transport]")
{
    using namespace shark;

    constexpr auto spacing = 0x1p511;
    constexpr auto duration = 0x1p1023;
    constexpr std::array<double, 2> bed{0.0, 0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        2>
        states{{
            {0x1p-2, 0x1.8p-567, 0.0},
            {0x1p-2, 0x1.8p-567, 0.0},
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 1,
            .cell_spacing = spacing,
        },
        bed,
        states);
    const auto original = grid;
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            duration,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared =
                    std::numeric_limits<double>::denorm_min(),
                .courant_number = 0.5,
                .dry_depth_threshold_meters =
                    std::numeric_limits<double>::denorm_min(),
                .maximum_substep_count = 1U,
            });

    REQUIRE(report);
    REQUIRE(report.value().substep_count == 1U);
    REQUIRE(
        report.value().maximum_observed_courant_number ==
        0.0);
    REQUIRE(
        report.value().cumulative_absolute_face_volume ==
        0x1.8p967);
    REQUIRE(grid != original);
    REQUIRE(
        grid.states[0].water_depth ==
        0x1.ffffffffffffep-3);
    REQUIRE(
        grid.states[1].water_depth ==
        0x1.0000000000001p-2);
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "a partially wet uneven lake preserves its dry shoreline",
    "[fluids][shallow-water][wet-dry][lake-at-rest][shoreline]")
{
    using namespace shark;

    constexpr auto threshold = 1.0 / 64.0;
    constexpr auto film_depth = 1.0 / 128.0;
    constexpr std::array<double, 9> bed{
        3.0, 2.0 - film_depth, 3.0,
        2.0 - film_depth, 0.0, 2.0 - film_depth,
        3.0, 2.0 - film_depth, 3.0,
    };
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        9>
        states{{
            {0.0, 0.0, 0.0},
            {film_depth, 0.0, 0.0},
            {0.0, 0.0, 0.0},
            {film_depth, 0.0, 0.0},
            {2.0, 0.0, 0.0},
            {film_depth, 0.0, 0.0},
            {0.0, 0.0, 0.0},
            {film_depth, 0.0, 0.0},
            {0.0, 0.0, 0.0},
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 3,
            .rows = 3,
            .cell_spacing = 1.0,
        },
        bed,
        states);
    const auto original = grid;

    for (std::uint32_t tick = 0U;
         tick < 60U;
         ++tick) {
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                1.0 / 60.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = threshold,
                });
        CAPTURE(tick);
        REQUIRE(report);
        REQUIRE(grid == original);
        REQUIRE(report.value().activation_count == 0U);
        REQUIRE(report.value().deactivation_count == 0U);
        REQUIRE(report.value().initial_active_cell_count == 1U);
        REQUIRE(
            report.value().
                initial_retained_film_cell_count == 4U);
        REQUIRE(
            report.value().initial_exact_dry_cell_count == 4U);
        REQUIRE(report.value().final_active_cell_count == 1U);
        REQUIRE(
            report.value().
                final_retained_film_cell_count == 4U);
        REQUIRE(
            report.value().final_exact_dry_cell_count == 4U);
        require_report_matches_grid(report.value(), grid);
    }
}

TEST_CASE(
    "a flat wet front activates an exact dry neighbor analytically",
    "[fluids][shallow-water][wet-dry][front][analytic]")
{
    using namespace shark;

    constexpr std::array<double, 2> depths{1.0, 0.0};
    auto grid = make_flat_grid(2, 1, depths);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.1,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .dry_depth_threshold_meters = 0.01,
                .maximum_substep_count = 1U,
            });

    REQUIRE(report);
    REQUIRE(grid.states[0].water_depth ==
        Catch::Approx(0.95).margin(1.0E-15));
    REQUIRE(grid.states[1].water_depth ==
        Catch::Approx(0.05).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_x ==
        Catch::Approx(0.025).margin(1.0E-15));
    REQUIRE(grid.states[1].momentum_x ==
        Catch::Approx(0.025).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_z == 0.0);
    REQUIRE(grid.states[1].momentum_z == 0.0);
    REQUIRE(report.value().initial_active_cell_count == 1U);
    REQUIRE(report.value().initial_exact_dry_cell_count == 1U);
    REQUIRE(report.value().final_active_cell_count == 2U);
    REQUIRE(report.value().final_exact_dry_cell_count == 0U);
    REQUIRE(report.value().activation_count == 1U);
    REQUIRE(report.value().deactivation_count == 0U);
    REQUIRE(
        report.value().cumulative_absolute_face_volume ==
        Catch::Approx(0.05).margin(1.0E-15));
    REQUIRE(
        report.value().
            cumulative_discarded_absolute_momentum_x ==
        0.0);
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "a two dimensional point source wets four cardinal neighbors",
    "[fluids][shallow-water][wet-dry][front][2d][symmetry]")
{
    using namespace shark;

    constexpr std::array<double, 9> depths{
        0.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 0.0,
    };
    auto grid = make_flat_grid(3, 3, depths);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.1,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .dry_depth_threshold_meters = 0.01,
                .maximum_substep_count = 1U,
            });
    REQUIRE(report);

    const auto center = cell_index(1U, 1U, 3U);
    const auto west = cell_index(0U, 1U, 3U);
    const auto east = cell_index(2U, 1U, 3U);
    const auto south = cell_index(1U, 0U, 3U);
    const auto north = cell_index(1U, 2U, 3U);
    REQUIRE(grid.states[center].water_depth ==
        Catch::Approx(0.8).margin(1.0E-15));
    for (const auto index : {west, east, south, north}) {
        REQUIRE(grid.states[index].water_depth ==
            Catch::Approx(0.05).margin(1.0E-15));
    }
    REQUIRE(grid.states[west].momentum_x ==
        Catch::Approx(-0.025).margin(1.0E-15));
    REQUIRE(grid.states[east].momentum_x ==
        Catch::Approx(0.025).margin(1.0E-15));
    REQUIRE(grid.states[south].momentum_z ==
        Catch::Approx(-0.025).margin(1.0E-15));
    REQUIRE(grid.states[north].momentum_z ==
        Catch::Approx(0.025).margin(1.0E-15));
    REQUIRE(grid.states[west].momentum_z == 0.0);
    REQUIRE(grid.states[east].momentum_z == 0.0);
    REQUIRE(grid.states[south].momentum_x == 0.0);
    REQUIRE(grid.states[north].momentum_x == 0.0);
    REQUIRE(grid.states[center].momentum_x == 0.0);
    REQUIRE(grid.states[center].momentum_z == 0.0);
    for (const auto index :
         {cell_index(0U, 0U, 3U),
          cell_index(2U, 0U, 3U),
          cell_index(0U, 2U, 3U),
          cell_index(2U, 2U, 3U)}) {
        REQUIRE(grid.states[index].water_depth == 0.0);
    }
    REQUIRE(report.value().activation_count == 4U);
    REQUIRE(report.value().final_active_cell_count == 5U);
    REQUIRE(report.value().final_exact_dry_cell_count == 4U);
    REQUIRE(
        report.value().cumulative_absolute_face_volume ==
        Catch::Approx(0.2).margin(1.0E-15));
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "activation uses a strict greater than threshold boundary",
    "[fluids][shallow-water][wet-dry][threshold][activation]")
{
    using namespace shark;

    constexpr auto threshold = 1.0 / 16.0;
    constexpr auto exact_time = 1.0 / 8.0;
    const std::array times{
        std::nextafter(exact_time, 0.0),
        exact_time,
        std::nextafter(
            exact_time,
            std::numeric_limits<double>::infinity()),
    };
    for (std::size_t case_index = 0U;
         case_index < times.size();
         ++case_index) {
        constexpr std::array<double, 2> depths{1.0, 0.0};
        auto grid = make_flat_grid(2, 1, depths);
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                times[case_index],
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = threshold,
                    .maximum_substep_count = 1U,
                });
        CAPTURE(case_index, times[case_index]);
        REQUIRE(report);
        const auto should_activate = case_index == 2U;
        if (case_index == 0U) {
            REQUIRE(grid.states[1].water_depth < threshold);
        } else if (case_index == 1U) {
            REQUIRE(grid.states[1].water_depth == threshold);
        } else {
            REQUIRE(grid.states[1].water_depth > threshold);
        }
        REQUIRE(report.value().activation_count ==
            (should_activate ? 1U : 0U));
        REQUIRE(report.value().final_active_cell_count ==
            (should_activate ? 2U : 1U));
        REQUIRE(
            report.value().final_retained_film_cell_count ==
            (should_activate ? 0U : 1U));
        REQUIRE(grid.states[1].momentum_x ==
            (should_activate
                ? Catch::Approx(0.25 * times[case_index])
                    .margin(1.0E-15)
                : Catch::Approx(0.0)));
        REQUIRE(
            report.value().
                cumulative_discarded_absolute_momentum_x ==
            (should_activate
                ? Catch::Approx(0.0)
                : Catch::Approx(
                    0.25 * times[case_index])));
        REQUIRE(
            report.value().
                cumulative_discarded_absolute_momentum_z ==
            0.0);
        require_report_matches_grid(report.value(), grid);
    }
}

TEST_CASE(
    "retreat retains film depth and reports projected momentum",
    "[fluids][shallow-water][wet-dry][retreat][analytic]")
{
    using namespace shark;

    constexpr std::array<double, 2> bed{0.0, 0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        2>
        states{{
            {0.11, 0.10, 0.0},
            {0.11, 0.10, 0.0},
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 1,
            .cell_spacing = 2.0,
        },
        bed,
        states);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.4,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .dry_depth_threshold_meters = 0.10,
                .maximum_substep_count = 1U,
            });
    REQUIRE(report);

    const auto expected_momentum =
        0.10 *
        (1.0 -
         0.2 * (0.10 / 0.11 + std::sqrt(0.11)));
    REQUIRE(grid.states[0].water_depth ==
        Catch::Approx(0.09).margin(1.0E-15));
    REQUIRE(grid.states[1].water_depth ==
        Catch::Approx(0.13).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_x == 0.0);
    REQUIRE(grid.states[0].momentum_z == 0.0);
    REQUIRE(grid.states[1].momentum_x ==
        Catch::Approx(expected_momentum).margin(1.0E-15));
    REQUIRE(grid.states[1].momentum_z == 0.0);
    REQUIRE(report.value().activation_count == 0U);
    REQUIRE(report.value().deactivation_count == 1U);
    REQUIRE(report.value().final_active_cell_count == 1U);
    REQUIRE(
        report.value().final_retained_film_cell_count == 1U);
    REQUIRE(report.value().final_retained_film_volume ==
        Catch::Approx(0.36).margin(1.0E-15));
    REQUIRE(
        report.value().
            cumulative_discarded_absolute_momentum_x ==
        Catch::Approx(4.0 * expected_momentum)
            .margin(1.0E-15));
    REQUIRE(
        report.value().
            cumulative_discarded_absolute_momentum_z ==
        0.0);
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "hydrostatic reconstruction blocks high shore and survives bed range",
    "[fluids][shallow-water][wet-dry][shoreline][range][analytic]")
{
    using namespace shark;

    SECTION("a dry bed above the waterline remains blocked")
    {
        constexpr std::array<double, 2> bed{0.0, 2.0};
        constexpr std::array<
            fluids::ShallowWaterConservedState,
            2>
            states{{
                {1.0, 0.0, 0.0},
                {0.0, 0.0, 0.0},
            }};
        auto grid = make_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 2,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            states);
        const auto original = grid;
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.1,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = 0.01,
                });
        REQUIRE(report);
        REQUIRE(grid == original);
        REQUIRE(report.value().activation_count == 0U);
        REQUIRE(
            report.value().
                cumulative_absolute_face_volume == 0.0);
        require_report_matches_grid(report.value(), grid);
    }

    SECTION("range-safe bed difference preserves the high-side depth")
    {
        constexpr std::array<double, 2> bed{
            -1.0E308,
            1.0E308,
        };
        constexpr std::array<
            fluids::ShallowWaterConservedState,
            2>
            states{{
                {1.0, 0.0, 0.0},
                {1.0, 0.0, 0.0},
            }};
        auto grid = make_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 2,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            states);
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.1,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = 0.01,
                    .maximum_substep_count = 1U,
                });
        REQUIRE(report);
        REQUIRE(grid.states[0].water_depth ==
            Catch::Approx(1.05).margin(1.0E-15));
        REQUIRE(grid.states[1].water_depth ==
            Catch::Approx(0.95).margin(1.0E-15));
        REQUIRE(grid.states[0].momentum_x ==
            Catch::Approx(-0.025).margin(1.0E-15));
        REQUIRE(grid.states[1].momentum_x ==
            Catch::Approx(-0.025).margin(1.0E-15));
        REQUIRE(
            report.value().
                cumulative_absolute_face_volume ==
            Catch::Approx(0.05).margin(1.0E-15));
        require_report_matches_grid(report.value(), grid);
    }
}

TEST_CASE(
    "dry dam fronts remain transposed while they activate cells",
    "[fluids][shallow-water][wet-dry][front][symmetry][determinism]")
{
    using namespace shark;

    constexpr std::array<double, 8> depths{
        1.0, 1.0, 1.0, 1.0,
        0.0, 0.0, 0.0, 0.0,
    };
    auto x_grid = make_flat_grid(8, 1, depths);
    auto z_grid = make_flat_grid(1, 8, depths);
    auto activation_count = 0U;
    for (std::uint32_t tick = 0U;
         tick < 30U;
         ++tick) {
        const auto x_report =
            fluids::advance_shallow_water_reference_grid(
                x_grid,
                1.0 / 60.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                });
        const auto z_report =
            fluids::advance_shallow_water_reference_grid(
                z_grid,
                1.0 / 60.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                });
        CAPTURE(tick);
        REQUIRE(x_report);
        REQUIRE(z_report);
        REQUIRE(x_report.value().substep_count ==
            z_report.value().substep_count);
        REQUIRE(x_report.value().activation_count ==
            z_report.value().activation_count);
        REQUIRE(x_report.value().deactivation_count ==
            z_report.value().deactivation_count);
        REQUIRE(
            x_report.value().
                cumulative_absolute_face_volume ==
            z_report.value().
                cumulative_absolute_face_volume);
        REQUIRE(
            x_report.value().
                cumulative_discarded_absolute_momentum_x ==
            z_report.value().
                cumulative_discarded_absolute_momentum_z);
        activation_count +=
            x_report.value().activation_count;
        require_report_matches_grid(x_report.value(), x_grid);
        require_report_matches_grid(z_report.value(), z_grid);
    }
    REQUIRE(activation_count > 0U);
    for (std::size_t index = 0;
         index < depths.size();
         ++index) {
        CAPTURE(index);
        REQUIRE(x_grid.states[index].water_depth ==
            z_grid.states[index].water_depth);
        REQUIRE(x_grid.states[index].momentum_x ==
            z_grid.states[index].momentum_z);
        REQUIRE(x_grid.states[index].momentum_z ==
            z_grid.states[index].momentum_x);
    }
}

TEST_CASE(
    "mixed two dimensional fronts stay deterministic and conservative",
    "[fluids][shallow-water][wet-dry][front][2d][mass][determinism]")
{
    using namespace shark;

    std::array<double, 64> bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        64>
        states{};
    for (std::uint32_t row = 0U;
         row < 8U;
         ++row) {
        for (std::uint32_t column = 0U;
             column < 8U;
             ++column) {
            const auto index =
                cell_index(column, row, 8U);
            bed[index] =
                0.01 *
                static_cast<double>(
                    (3U * column + 5U * row) % 5U);
            if (column < 4U) {
                states[index] = {
                    .water_depth =
                        0.8 +
                        0.05 *
                            static_cast<double>(
                                (column + row) % 4U),
                    .momentum_x =
                        0.01 *
                        static_cast<double>(column + 1U),
                    .momentum_z =
                        0.005 *
                        (static_cast<double>(row) - 3.0),
                };
            }
        }
    }
    auto first = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 8,
            .rows = 8,
            .cell_spacing = 1.0,
        },
        bed,
        states);
    auto second = first;
    const auto initial =
        fluids::inspect_shallow_water_reference_grid(first);
    REQUIRE(initial);
    auto activation_count = 0U;
    for (std::uint32_t tick = 0U;
         tick < 60U;
         ++tick) {
        const auto first_report =
            fluids::advance_shallow_water_reference_grid(
                first,
                1.0 / 60.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                });
        const auto second_report =
            fluids::advance_shallow_water_reference_grid(
                second,
                1.0 / 60.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                });
        CAPTURE(tick);
        REQUIRE(first_report);
        REQUIRE(second_report);
        REQUIRE(first_report.value() ==
            second_report.value());
        REQUIRE(first == second);
        REQUIRE(
            fluids::validate_shallow_water_reference_grid(
                first));
        activation_count +=
            first_report.value().activation_count;
        require_report_matches_grid(
            first_report.value(),
            first);
    }
    REQUIRE(activation_count > 0U);
    const auto final =
        fluids::inspect_shallow_water_reference_grid(first);
    REQUIRE(final);
    const auto tolerance =
        65'536.0 *
        std::numeric_limits<double>::epsilon() *
        initial.value().water_volume;
    REQUIRE(std::abs(
        final.value().water_volume -
        initial.value().water_volume) <= tolerance);
}

TEST_CASE(
    "wet dry candidate work and momentum projection roll back together",
    "[fluids][shallow-water][wet-dry][rollback][substeps]")
{
    using namespace shark;

    SECTION("an activating candidate is not committed on budget failure")
    {
        constexpr std::array<double, 2> depths{1.0, 0.0};
        auto grid = make_flat_grid(2, 1, depths);
        const auto original = grid;
        const auto result =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.5,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = 0.01,
                    .maximum_substep_count = 1U,
                });
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(grid == original);
    }

    SECTION("initial near dry projection is transactional")
    {
        constexpr std::array<double, 1> bed{0.0};
        constexpr std::array<
            fluids::ShallowWaterConservedState,
            1>
            states{{
                {0.05, 0.02, -0.01},
            }};
        auto grid = make_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            states);
        const auto original = grid;
        const auto result =
            fluids::advance_shallow_water_reference_grid(
                grid,
                2.0,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = 0.1,
                    .maximum_substep_count = 1U,
                });
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(grid == original);
    }

    SECTION("substep near dry projection is transactional")
    {
        constexpr std::array<double, 2> bed{0.0, 0.0};
        constexpr std::array<
            fluids::ShallowWaterConservedState,
            2>
            states{{
                {0.11, 0.10, 0.0},
                {0.11, 0.10, 0.0},
            }};
        auto grid = make_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 2,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            states);
        const auto original = grid;
        const auto result =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.5,
                fluids::ShallowWaterAdvanceSettings{
                    .gravity_meters_per_second_squared = 1.0,
                    .courant_number = 0.5,
                    .dry_depth_threshold_meters = 0.1,
                    .maximum_substep_count = 1U,
                });
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(grid == original);
    }
}
