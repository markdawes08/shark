#include <shark/fluids/shallow_water_reference.hpp>
#include <shark/fluids/shallow_water_solver.hpp>

#include <shark/core/error.hpp>
#include <shark/terrain/height_tile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace {

[[nodiscard]] constexpr std::size_t cell_index(
    const std::uint32_t column,
    const std::uint32_t row,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(row) *
        columns + column;
}

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
    const std::span<const double> depths,
    const double cell_spacing = 1.0)
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
            .cell_spacing = cell_spacing,
        },
        std::span<const double>{bed.data(), count},
        std::span<
            const fluids::ShallowWaterConservedState>{
                states.data(),
                count});
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
    REQUIRE(report.cell_count == grid.cell_count);
    REQUIRE(report.cell_count ==
        diagnostics.value().cell_count);
    REQUIRE(report.advanced_seconds ==
        report.requested_seconds);
    REQUIRE(report.minimum_substep_seconds > 0.0);
    REQUIRE(report.minimum_substep_seconds <=
        report.maximum_substep_seconds);
    REQUIRE(report.final_water_volume ==
        diagnostics.value().water_volume);
    REQUIRE(report.final_minimum_water_depth ==
        diagnostics.value().minimum_water_depth);
    REQUIRE(report.final_maximum_absolute_momentum_x ==
        diagnostics.value().maximum_absolute_momentum_x);
    REQUIRE(report.final_maximum_absolute_momentum_z ==
        diagnostics.value().maximum_absolute_momentum_z);
    REQUIRE(
        report.maximum_absolute_volume_balance_residual >=
        std::abs(report.volume_balance_residual));
}

[[nodiscard]] shark::terrain::HeightTile
make_uneven_canonical_tile()
{
    return shark::terrain::HeightTile{
        .sample_columns = 5,
        .sample_rows = 4,
        .sample_spacing = 2.0F,
        .origin = {-8.0F, -4.0F, 10.0F},
        .height_offsets = {
            0.0F, 1.0F, 0.0F, 2.0F, 1.0F,
            1.0F, 3.0F, 2.0F, 4.0F, 2.0F,
            -1.0F, 0.0F, 2.0F, 3.0F, 1.0F,
            0.0F, 2.0F, 1.0F, 5.0F, 3.0F,
        },
    };
}

[[nodiscard]] std::array<
    double,
    shark::fluids::shallow_water_reference_cell_capacity>
canonical_cell_average_bed(
    const shark::terrain::HeightTileSurface& surface)
{
    const auto& tile = surface.tile();
    std::array<
        double,
        shark::fluids::shallow_water_reference_cell_capacity>
        bed{};
    const auto columns = tile.sample_columns - 1U;
    const auto rows = tile.sample_rows - 1U;
    for (std::uint32_t row = 0;
         row < rows;
         ++row) {
        for (std::uint32_t column = 0;
             column < columns;
             ++column) {
            const auto height =
                [&tile](
                    const std::uint32_t x,
                    const std::uint32_t z) {
                    return
                        static_cast<double>(tile.origin.y) +
                        tile.height_offsets[
                            cell_index(
                                x,
                                z,
                                tile.sample_columns)];
                };
            const auto b00 = height(column, row);
            const auto b01 = height(column, row + 1U);
            const auto b10 = height(column + 1U, row);
            const auto b11 =
                height(column + 1U, row + 1U);
            bed[cell_index(column, row, columns)] =
                (2.0 * b00 + b01 + b10 + 2.0 * b11) /
                6.0;
        }
    }
    return bed;
}

[[nodiscard]] shark::fluids::ShallowWaterReferenceGrid
make_uneven_lake()
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(
            make_uneven_canonical_tile());
    REQUIRE(surface_result);
    const auto surface =
        std::move(surface_result).value();
    const auto bed =
        canonical_cell_average_bed(surface);
    const auto& tile = surface.tile();
    const fluids::ShallowWaterGridConfig config{
        .columns = tile.sample_columns - 1U,
        .rows = tile.sample_rows - 1U,
        .origin_x = tile.origin.x,
        .origin_z = tile.origin.z,
        .cell_spacing = tile.sample_spacing,
    };
    const auto count =
        static_cast<std::size_t>(config.columns) *
        config.rows;
    const auto result =
        fluids::make_lake_at_rest_reference_grid(
            config,
            std::span<const double>{bed.data(), count},
            4.0);
    REQUIRE(result);
    return result.value();
}

} // namespace

TEST_CASE(
    "Rusanov flux advances one flat dam-break interface analytically",
    "[fluids][shallow-water][advance][flux]")
{
    using namespace shark;

    constexpr std::array<double, 2> depths{4.0, 1.0};
    auto grid = make_flat_grid(2, 1, depths, 2.0);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.2,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .maximum_substep_count = 1,
            });
    REQUIRE(report);
    REQUIRE(report.value().substep_count == 1);
    REQUIRE(report.value().requested_seconds == 0.2);
    REQUIRE(report.value().advanced_seconds == 0.2);
    REQUIRE(
        report.value().
            maximum_observed_courant_number ==
        Catch::Approx(0.4).epsilon(0.0).margin(1.0E-15));

    REQUIRE(grid.states[0].water_depth ==
        Catch::Approx(3.7).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[1].water_depth ==
        Catch::Approx(1.3).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_x ==
        Catch::Approx(0.375).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[1].momentum_x ==
        Catch::Approx(0.375).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_z == 0.0);
    REQUIRE(grid.states[1].momentum_z == 0.0);
    REQUIRE(report.value().initial_water_volume == 20.0);
    REQUIRE(report.value().final_water_volume == 20.0);
    REQUIRE(
        report.value().cumulative_absolute_face_volume ==
        Catch::Approx(1.2).epsilon(0.0).margin(1.0E-15));
    require_closed_ledger(report.value());
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "CFL scheduling partitions the complete requested interval",
    "[fluids][shallow-water][advance][cfl]")
{
    using namespace shark;

    constexpr std::array<double, 2> depths{1.0, 1.0};
    auto grid = make_flat_grid(2, 1, depths);
    const auto original = grid;
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.6,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .maximum_substep_count = 3,
            });
    REQUIRE(report);
    REQUIRE(grid == original);
    REQUIRE(report.value().substep_count == 3);
    REQUIRE(report.value().requested_seconds == 0.6);
    REQUIRE(report.value().advanced_seconds == 0.6);
    REQUIRE(
        report.value().minimum_substep_seconds ==
        Catch::Approx(0.1).epsilon(0.0).margin(1.0E-15));
    REQUIRE(
        report.value().maximum_substep_seconds ==
        Catch::Approx(0.25).epsilon(0.0).margin(1.0E-15));
    REQUIRE(
        report.value().
            maximum_observed_courant_number ==
        Catch::Approx(0.5).epsilon(0.0).margin(1.0E-15));
    REQUIRE(report.value().cumulative_absolute_face_volume ==
        0.0);
    require_closed_ledger(report.value());
}

TEST_CASE(
    "hydrostatic reconstruction preserves an uneven lake at rest",
    "[fluids][shallow-water][advance][lake-at-rest]")
{
    using namespace shark;

    auto grid = make_uneven_lake();
    const auto original = grid;
    const auto initial =
        fluids::inspect_shallow_water_reference_grid(grid);
    REQUIRE(initial);

    for (std::uint32_t tick = 0;
         tick < 60U;
         ++tick) {
        const auto report =
            fluids::advance_shallow_water_reference_grid(
                grid,
                1.0 / 60.0);
        CAPTURE(tick);
        REQUIRE(report);
        REQUIRE(
            report.value().
                maximum_observed_courant_number <=
            0.45 +
                32.0 *
                    std::numeric_limits<double>::epsilon());
        require_closed_ledger(report.value());
    }

    REQUIRE(grid.config == original.config);
    REQUIRE(grid.bed_elevations ==
        original.bed_elevations);
    REQUIRE(grid.cell_count == original.cell_count);
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        CAPTURE(index);
        REQUIRE(grid.states[index].water_depth ==
            Catch::Approx(
                original.states[index].water_depth)
                .epsilon(0.0)
                .margin(2.0E-12));
        REQUIRE(std::abs(grid.states[index].momentum_x) <=
            2.0E-12);
        REQUIRE(std::abs(grid.states[index].momentum_z) <=
            2.0E-12);
    }
    for (std::size_t index = grid.cell_count;
         index <
            fluids::shallow_water_reference_cell_capacity;
         ++index) {
        REQUIRE(grid.states[index] ==
            original.states[index]);
    }

    const auto final =
        fluids::inspect_shallow_water_reference_grid(grid);
    REQUIRE(final);
    const auto tolerance =
        4'096.0 *
        std::numeric_limits<double>::epsilon() *
        std::max(1.0, initial.value().water_volume);
    REQUIRE(std::abs(
        final.value().water_volume -
        initial.value().water_volume) <= tolerance);
    REQUIRE(
        final.value().maximum_free_surface_elevation -
            final.value().minimum_free_surface_elevation <=
        4.0E-12);
}

TEST_CASE(
    "sealed fully wet dam break stays positive and deterministic",
    "[fluids][shallow-water][advance][dam-break][determinism]")
{
    using namespace shark;

    constexpr std::array<double, 8> depths{
        2.0,
        2.0,
        2.0,
        2.0,
        1.0,
        1.0,
        1.0,
        1.0,
    };
    auto first = make_flat_grid(8, 1, depths);
    auto second = first;
    const auto initial =
        fluids::inspect_shallow_water_reference_grid(first);
    REQUIRE(initial);

    for (std::uint32_t tick = 0;
         tick < 30U;
         ++tick) {
        const auto first_report =
            fluids::advance_shallow_water_reference_grid(
                first,
                1.0 / 15.0);
        const auto second_report =
            fluids::advance_shallow_water_reference_grid(
                second,
                1.0 / 15.0);
        CAPTURE(tick);
        REQUIRE(first_report);
        REQUIRE(second_report);
        REQUIRE(first_report.value() ==
            second_report.value());
        REQUIRE(first == second);
        REQUIRE(first_report.value().substep_count >= 2U);
        REQUIRE(
            first_report.value().
                maximum_observed_courant_number <=
            0.45 +
                32.0 *
                    std::numeric_limits<double>::epsilon());
        REQUIRE(
            first_report.value().
                final_minimum_water_depth > 0.0);
        require_closed_ledger(first_report.value());
        require_report_matches_grid(
            first_report.value(),
            first);
        REQUIRE(
            fluids::validate_shallow_water_reference_grid(
                first));
    }

    const auto final =
        fluids::inspect_shallow_water_reference_grid(first);
    REQUIRE(final);
    REQUIRE(final.value().minimum_water_depth > 0.0);
    REQUIRE(final.value().maximum_absolute_momentum_x >
        0.0);
    const auto tolerance =
        16'384.0 *
        std::numeric_limits<double>::epsilon() *
        std::max(1.0, initial.value().water_volume);
    REQUIRE(std::abs(
        final.value().water_volume -
        initial.value().water_volume) <= tolerance);
    REQUIRE(first.states[3].water_depth != depths[3]);
    REQUIRE(first.states[4].water_depth != depths[4]);
}

TEST_CASE(
    "one-dimensional X and Z dam breaks remain transposed",
    "[fluids][shallow-water][advance][symmetry][1d]")
{
    using namespace shark;

    constexpr std::array<double, 8> depths{
        2.0,
        2.0,
        2.0,
        2.0,
        1.0,
        1.0,
        1.0,
        1.0,
    };
    auto x_grid = make_flat_grid(8, 1, depths);
    auto z_grid = make_flat_grid(1, 8, depths);
    for (std::uint32_t tick = 0;
         tick < 30U;
         ++tick) {
        const auto x_report =
            fluids::advance_shallow_water_reference_grid(
                x_grid,
                1.0 / 60.0);
        const auto z_report =
            fluids::advance_shallow_water_reference_grid(
                z_grid,
                1.0 / 60.0);
        REQUIRE(x_report);
        REQUIRE(z_report);
        REQUIRE(x_report.value().substep_count ==
            z_report.value().substep_count);
    }

    for (std::size_t index = 0;
         index < depths.size();
         ++index) {
        CAPTURE(index);
        REQUIRE(x_grid.states[index].water_depth ==
            Catch::Approx(
                z_grid.states[index].water_depth)
                .epsilon(1.0E-13));
        REQUIRE(x_grid.states[index].momentum_x ==
            Catch::Approx(
                z_grid.states[index].momentum_z)
                .epsilon(1.0E-13)
                .margin(1.0E-14));
        REQUIRE(x_grid.states[index].momentum_z ==
            Catch::Approx(
                z_grid.states[index].momentum_x)
                .epsilon(1.0E-13)
                .margin(1.0E-14));
    }
}

TEST_CASE(
    "two-dimensional solver remains invariant under X Z transpose",
    "[fluids][shallow-water][advance][symmetry][2d]")
{
    using namespace shark;

    constexpr std::array<double, 6> bed{
        0.0,
        0.1,
        0.2,
        -0.1,
        0.05,
        0.15,
    };
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        6>
        states{{
            {2.0, 0.10, -0.04},
            {2.2, -0.05, 0.03},
            {1.8, 0.02, 0.06},
            {2.1, -0.03, -0.02},
            {1.9, 0.07, 0.01},
            {2.3, -0.04, -0.05},
        }};
    auto original = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 3,
            .rows = 2,
            .cell_spacing = 1.0,
        },
        bed,
        states);

    std::array<double, 6> transposed_bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        6>
        transposed_states{};
    for (std::uint32_t row = 0;
         row < 2U;
         ++row) {
        for (std::uint32_t column = 0;
             column < 3U;
             ++column) {
            const auto source =
                cell_index(column, row, 3U);
            const auto target =
                cell_index(row, column, 2U);
            transposed_bed[target] = bed[source];
            transposed_states[target] =
                fluids::ShallowWaterConservedState{
                    .water_depth =
                        states[source].water_depth,
                    .momentum_x =
                        states[source].momentum_z,
                    .momentum_z =
                        states[source].momentum_x,
                };
        }
    }
    auto transposed = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 3,
            .cell_spacing = 1.0,
        },
        transposed_bed,
        transposed_states);

    for (std::uint32_t step = 0;
         step < 4U;
         ++step) {
        REQUIRE(
            fluids::advance_shallow_water_reference_grid(
                original,
                0.01));
        REQUIRE(
            fluids::advance_shallow_water_reference_grid(
                transposed,
                0.01));
    }
    for (std::uint32_t row = 0;
         row < 2U;
         ++row) {
        for (std::uint32_t column = 0;
             column < 3U;
             ++column) {
            const auto source =
                cell_index(column, row, 3U);
            const auto target =
                cell_index(row, column, 2U);
            CAPTURE(source, target);
            REQUIRE(
                original.states[source].water_depth ==
                Catch::Approx(
                    transposed.states[target].water_depth)
                    .epsilon(2.0E-13));
            REQUIRE(
                original.states[source].momentum_x ==
                Catch::Approx(
                    transposed.states[target].momentum_z)
                    .epsilon(2.0E-13)
                    .margin(2.0E-14));
            REQUIRE(
                original.states[source].momentum_z ==
                Catch::Approx(
                    transposed.states[target].momentum_x)
                    .epsilon(2.0E-13)
                    .margin(2.0E-14));
        }
    }
}

TEST_CASE(
    "full-capacity nonlinear grid advances conservatively in two dimensions",
    "[fluids][shallow-water][advance][2d][capacity][determinism]")
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
    for (std::uint32_t row = 0U;
         row < fluids::shallow_water_reference_max_rows;
         ++row) {
        for (std::uint32_t column = 0U;
             column <
                 fluids::shallow_water_reference_max_columns;
             ++column) {
            const auto index =
                cell_index(
                    column,
                    row,
                    fluids::
                        shallow_water_reference_max_columns);
            bed[index] =
                0.01 *
                static_cast<double>(
                    (3U * column + 5U * row) % 7U);
            states[index] = {
                .water_depth =
                    1.0 +
                    0.04 *
                        static_cast<double>(
                            (column + 2U * row) % 6U),
                .momentum_x =
                    0.015 *
                        (static_cast<double>(column) - 3.0) +
                    0.003 * static_cast<double>(row),
                .momentum_z =
                    -0.012 *
                        (static_cast<double>(row) - 4.0) +
                    0.002 * static_cast<double>(column),
            };
        }
    }

    auto first = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns =
                fluids::shallow_water_reference_max_columns,
            .rows = fluids::shallow_water_reference_max_rows,
            .cell_spacing = 0.75,
        },
        bed,
        states);
    auto second = first;
    const auto original = first;
    const auto first_report =
        fluids::advance_shallow_water_reference_grid(
            first,
            0.5,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
            });
    const auto second_report =
        fluids::advance_shallow_water_reference_grid(
            second,
            0.5,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
            });

    REQUIRE(first_report);
    REQUIRE(second_report);
    REQUIRE(first_report.value() == second_report.value());
    REQUIRE(first == second);
    REQUIRE(first != original);
    REQUIRE(first_report.value().substep_count > 1U);
    REQUIRE(first_report.value().final_minimum_water_depth >
        0.0);
    REQUIRE(
        first_report.value().
            final_maximum_absolute_momentum_x > 0.0);
    REQUIRE(
        first_report.value().
            final_maximum_absolute_momentum_z > 0.0);
    REQUIRE(
        fluids::validate_shallow_water_reference_grid(first));
    require_closed_ledger(first_report.value());
    require_report_matches_grid(first_report.value(), first);
}

TEST_CASE(
    "reflective wall ordering applies the expected momentum impulse",
    "[fluids][shallow-water][advance][boundary][solid]")
{
    using namespace shark;

    constexpr std::array<double, 2> bed{0.0, 0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        2>
        states{{
            {1.0, 0.2, 0.1},
            {1.0, 0.2, 0.1},
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
            0.05,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
            });
    REQUIRE(report);
    REQUIRE(grid.states[0].water_depth ==
        Catch::Approx(0.99).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[1].water_depth ==
        Catch::Approx(1.01).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_x ==
        Catch::Approx(0.188).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[1].momentum_x ==
        Catch::Approx(0.188).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[0].momentum_z ==
        Catch::Approx(0.088).epsilon(0.0).margin(1.0E-15));
    REQUIRE(grid.states[1].momentum_z ==
        Catch::Approx(0.09).epsilon(0.0).margin(1.0E-15));
    require_closed_ledger(report.value());
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "tiny waves use the requested interval when CFL reciprocal overflows",
    "[fluids][shallow-water][advance][cfl][range]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{{
            {
                std::numeric_limits<double>::denorm_min(),
                0.0,
                0.0,
            },
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 1.0E154,
        },
        bed,
        states);
    const auto original = grid;
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            1.0,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .maximum_substep_count = 1U,
            });

    REQUIRE(report);
    REQUIRE(grid == original);
    REQUIRE(report.value().substep_count == 1U);
    REQUIRE(
        report.value().maximum_observed_courant_number >
        0.0);
    REQUIRE(report.value().volume_balance_tolerance <
        report.value().initial_water_volume);
    require_closed_ledger(report.value());
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "CFL axes scale before a representable rate is combined",
    "[fluids][shallow-water][advance][cfl][range]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{{
            {1.0E-308, 0.9, 0.9},
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 1.0E154,
        },
        bed,
        states);
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            1.0E-156,
            fluids::ShallowWaterAdvanceSettings{
                .maximum_substep_count = 1U,
            });

    REQUIRE(report);
    REQUIRE(report.value().substep_count == 1U);
    REQUIRE(
        report.value().maximum_observed_courant_number >
        0.0);
    REQUIRE(
        report.value().maximum_observed_courant_number <
        0.5);
    REQUIRE(grid.states[0].water_depth == states[0].water_depth);
    REQUIRE(grid.states[0].momentum_x > 0.0);
    REQUIRE(grid.states[0].momentum_x < states[0].momentum_x);
    REQUIRE(grid.states[0].momentum_x ==
        grid.states[0].momentum_z);
    require_closed_ledger(report.value());
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "Rusanov mean accepts representable same-sign pressure fluxes",
    "[fluids][shallow-water][advance][flux][range]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{{
            {1.0E154, 0.0, 0.0},
        }};
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 1.0E77,
        },
        bed,
        states);
    const auto original = grid;
    const auto report =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.01,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 2.5,
                .courant_number = 0.5,
                .maximum_substep_count = 1U,
            });

    REQUIRE(report);
    REQUIRE(grid == original);
    require_closed_ledger(report.value());
    require_report_matches_grid(report.value(), grid);
}

TEST_CASE(
    "advance rejects invalid requests without changing the grid",
    "[fluids][shallow-water][advance][validation][rollback]")
{
    using namespace shark;

    constexpr std::array<double, 2> depths{1.0, 1.0};
    const auto valid = make_flat_grid(2, 1, depths);

    const std::array invalid_times{
        0.0,
        -1.0,
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    for (const auto time : invalid_times) {
        auto grid = valid;
        const auto result =
            fluids::advance_shallow_water_reference_grid(
                grid,
                time);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(grid == valid);
    }

    const std::array invalid_settings{
        fluids::ShallowWaterAdvanceSettings{
            .gravity_meters_per_second_squared = 0.0,
        },
        fluids::ShallowWaterAdvanceSettings{
            .gravity_meters_per_second_squared = -1.0,
        },
        fluids::ShallowWaterAdvanceSettings{
            .gravity_meters_per_second_squared =
                std::numeric_limits<double>::infinity(),
        },
        fluids::ShallowWaterAdvanceSettings{
            .gravity_meters_per_second_squared =
                std::numeric_limits<double>::quiet_NaN(),
        },
        fluids::ShallowWaterAdvanceSettings{
            .courant_number = 0.0,
        },
        fluids::ShallowWaterAdvanceSettings{
            .courant_number = -0.1,
        },
        fluids::ShallowWaterAdvanceSettings{
            .courant_number =
                std::numeric_limits<double>::infinity(),
        },
        fluids::ShallowWaterAdvanceSettings{
            .courant_number = 0.5000001,
        },
        fluids::ShallowWaterAdvanceSettings{
            .courant_number =
                std::numeric_limits<double>::quiet_NaN(),
        },
        fluids::ShallowWaterAdvanceSettings{
            .maximum_substep_count = 0,
        },
        fluids::ShallowWaterAdvanceSettings{
            .maximum_substep_count =
                fluids::
                    maximum_shallow_water_substep_count +
                1U,
        },
    };
    for (const auto& settings : invalid_settings) {
        auto grid = valid;
        const auto result =
            fluids::advance_shallow_water_reference_grid(
                grid,
                0.1,
                settings);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(grid == valid);
    }

    auto corrupt = valid;
    corrupt.states[corrupt.cell_count].water_depth = 1.0;
    const auto corrupt_before = corrupt;
    REQUIRE_FALSE(
        fluids::advance_shallow_water_reference_grid(
            corrupt,
            0.1));
    REQUIRE(corrupt == corrupt_before);
}

TEST_CASE(
    "dry state and dry reconstruction remain deferred to W-004",
    "[fluids][shallow-water][advance][validation][dry][rollback]")
{
    using namespace shark;

    constexpr std::array<double, 1> dry_depth{0.0};
    auto dry = make_flat_grid(1, 1, dry_depth);
    const auto dry_original = dry;
    const auto dry_result =
        fluids::advance_shallow_water_reference_grid(
            dry,
            0.1);
    REQUIRE_FALSE(dry_result);
    REQUIRE(dry_result.error().code() ==
        core::ErrorCode::unsupported);
    REQUIRE(dry == dry_original);

    constexpr std::array<double, 2> bed{0.0, 10.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        2>
        states{{
            {1.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
        }};
    auto blocked = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 1,
            .cell_spacing = 1.0,
        },
        bed,
        states);
    const auto blocked_original = blocked;
    const auto blocked_result =
        fluids::advance_shallow_water_reference_grid(
            blocked,
            0.1);
    REQUIRE_FALSE(blocked_result);
    REQUIRE(blocked_result.error().code() ==
        core::ErrorCode::unsupported);
    REQUIRE(blocked == blocked_original);
}

TEST_CASE(
    "substep exhaustion rolls back work completed on the candidate",
    "[fluids][shallow-water][advance][substeps][rollback]")
{
    using namespace shark;

    constexpr std::array<double, 2> depths{4.0, 1.0};
    auto grid = make_flat_grid(2, 1, depths);
    const auto original = grid;
    const auto result =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.3,
            fluids::ShallowWaterAdvanceSettings{
                .gravity_meters_per_second_squared = 1.0,
                .courant_number = 0.5,
                .maximum_substep_count = 2,
            });
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(grid == original);
}

TEST_CASE(
    "aggregate volume overflow fails preflight without mutation",
    "[fluids][shallow-water][advance][overflow][rollback]")
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
    for (auto& state : states) {
        state.water_depth = 1.0E154;
    }
    auto grid = make_grid(
        fluids::ShallowWaterGridConfig{
            .columns =
                fluids::shallow_water_reference_max_columns,
            .rows = fluids::shallow_water_reference_max_rows,
            .cell_spacing = 3.2E76,
        },
        bed,
        states);
    const auto original = grid;
    const auto result =
        fluids::advance_shallow_water_reference_grid(
            grid,
            0.01);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() ==
        core::ErrorCode::invalid_state);
    REQUIRE(grid == original);
}

TEST_CASE(
    "arithmetic overflow rolls back the complete advance",
    "[fluids][shallow-water][advance][overflow][rollback]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        states{{
            {
                1.0E154,
                0.0,
                0.0,
            },
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
            0.01);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() ==
        core::ErrorCode::invalid_state);
    REQUIRE(grid == original);
}
