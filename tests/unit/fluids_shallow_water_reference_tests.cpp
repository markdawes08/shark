#include <shark/fluids/shallow_water_reference.hpp>

#include <shark/terrain/height_tile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace {

[[nodiscard]] constexpr shark::fluids::ShallowWaterGridConfig
test_config()
{
    return shark::fluids::ShallowWaterGridConfig{
        .columns = 3,
        .rows = 2,
        .origin_x = -4.0,
        .origin_z = 6.0,
        .cell_spacing = 2.0,
    };
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

[[nodiscard]] constexpr std::size_t terrain_index(
    const std::uint32_t column,
    const std::uint32_t row,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(row) *
        columns + column;
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
    const auto cell_columns = tile.sample_columns - 1U;
    const auto cell_rows = tile.sample_rows - 1U;
    for (std::uint32_t row = 0;
         row < cell_rows;
         ++row) {
        for (std::uint32_t column = 0;
             column < cell_columns;
             ++column) {
            const auto b00 =
                static_cast<double>(tile.origin.y) +
                tile.height_offsets[
                    terrain_index(
                        column,
                        row,
                        tile.sample_columns)];
            const auto b01 =
                static_cast<double>(tile.origin.y) +
                tile.height_offsets[
                    terrain_index(
                        column,
                        row + 1U,
                        tile.sample_columns)];
            const auto b10 =
                static_cast<double>(tile.origin.y) +
                tile.height_offsets[
                    terrain_index(
                        column + 1U,
                        row,
                        tile.sample_columns)];
            const auto b11 =
                static_cast<double>(tile.origin.y) +
                tile.height_offsets[
                    terrain_index(
                        column + 1U,
                        row + 1U,
                        tile.sample_columns)];

            // The canonical LOD0 split has two equal-area triangles:
            // (b00,b01,b11) and (b00,b11,b10).
            bed[terrain_index(
                column,
                row,
                cell_columns)] =
                (2.0 * b00 + b01 + b10 + 2.0 * b11) /
                6.0;
        }
    }
    return bed;
}

[[nodiscard]] bool positive_zero(const double value) noexcept
{
    return std::bit_cast<std::uint64_t>(value) == 0U;
}

} // namespace

TEST_CASE(
    "shallow-water reference grid owns one canonical row-major prefix",
    "[fluids][shallow-water][reference][contract]")
{
    using namespace shark;

    constexpr auto config = test_config();
    constexpr std::array<double, 6> bed{
        -3.0,
        -2.0,
        -1.0,
        0.0,
        1.0,
        2.0,
    };
    constexpr std::array<fluids::ShallowWaterConservedState, 6>
        states{{
            {1.0, 0.0, 0.0},
            {2.0, 1.0, -1.0},
            {3.0, 2.0, -2.0},
            {4.0, 3.0, -3.0},
            {5.0, 4.0, -4.0},
            {6.0, 5.0, -5.0},
        }};

    const auto result =
        fluids::make_shallow_water_reference_grid(
            config,
            bed,
            states);
    REQUIRE(result);
    const auto& grid = result.value();
    REQUIRE(grid.config == config);
    REQUIRE(grid.cell_count == 6);
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        CAPTURE(index);
        REQUIRE(grid.bed_elevations[index] == bed[index]);
        REQUIRE(grid.states[index] == states[index]);
    }
    for (std::size_t index = grid.cell_count;
         index <
            fluids::shallow_water_reference_cell_capacity;
         ++index) {
        CAPTURE(index);
        REQUIRE(positive_zero(grid.bed_elevations[index]));
        REQUIRE(positive_zero(
            grid.states[index].water_depth));
        REQUIRE(positive_zero(
            grid.states[index].momentum_x));
        REQUIRE(positive_zero(
            grid.states[index].momentum_z));
    }
    REQUIRE(fluids::validate_shallow_water_reference_grid(
        grid));

    const auto first_center =
        fluids::shallow_water_reference_cell_center(
            config,
            0,
            0);
    REQUIRE(first_center);
    REQUIRE(first_center.value() ==
        fluids::ShallowWaterCellCenter{-3.0, 7.0});
    const auto last_center =
        fluids::shallow_water_reference_cell_center(
            config,
            2,
            1);
    REQUIRE(last_center);
    REQUIRE(last_center.value() ==
        fluids::ShallowWaterCellCenter{1.0, 9.0});
    REQUIRE_FALSE(
        fluids::shallow_water_reference_cell_center(
            config,
            3,
            0));
    REQUIRE_FALSE(
        fluids::shallow_water_reference_cell_center(
            config,
            0,
            2));
}

TEST_CASE(
    "shallow-water reference capacity accepts its exact bounds",
    "[fluids][shallow-water][reference][capacity]")
{
    using namespace shark;

    constexpr std::array<double, 1> one_bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        one_state{{{0.0, 0.0, 0.0}}};
    const auto one =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            one_bed,
            one_state);
    REQUIRE(one);
    REQUIRE(one.value().cell_count == 1);

    std::array<
        double,
        fluids::shallow_water_reference_cell_capacity>
        full_bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        fluids::shallow_water_reference_cell_capacity>
        full_states{};
    const auto full =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns =
                    fluids::
                        shallow_water_reference_max_columns,
                .rows =
                    fluids::shallow_water_reference_max_rows,
                .cell_spacing = 1.0,
            },
            full_bed,
            full_states);
    REQUIRE(full);
    REQUIRE(full.value().cell_count ==
        fluids::shallow_water_reference_cell_capacity);

    auto too_wide = full.value().config;
    ++too_wide.columns;
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            too_wide,
            full_bed,
            full_states));
    auto too_tall = full.value().config;
    ++too_tall.rows;
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            too_tall,
            full_bed,
            full_states));
}

TEST_CASE(
    "shallow-water construction rejects malformed finite-volume state",
    "[fluids][shallow-water][reference][validation]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        state{{{1.0, 0.0, 0.0}}};
    constexpr auto valid_config =
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 1.0,
        };

    const std::array invalid_configs{
        fluids::ShallowWaterGridConfig{
            .rows = 1,
            .cell_spacing = 1.0,
        },
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing = 0.0,
        },
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .origin_x =
                std::numeric_limits<double>::infinity(),
            .cell_spacing = 1.0,
        },
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .origin_x =
                std::numeric_limits<double>::max(),
            .cell_spacing =
                std::numeric_limits<double>::max(),
        },
        fluids::ShallowWaterGridConfig{
            .columns = 1,
            .rows = 1,
            .cell_spacing =
                std::numeric_limits<double>::denorm_min(),
        },
        fluids::ShallowWaterGridConfig{
            .columns = 8,
            .rows = 1,
            .origin_x = 1.0E16,
            .cell_spacing = 1.0,
        },
    };
    for (const auto& config : invalid_configs) {
        REQUIRE_FALSE(
            fluids::make_shallow_water_reference_grid(
                config,
                bed,
                state));
    }

    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            valid_config,
            std::span<const double>{},
            state));
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            valid_config,
            bed,
            std::span<
                const fluids::ShallowWaterConservedState>{}));

    const std::array invalid_beds{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    for (const auto invalid_bed : invalid_beds) {
        const std::array malformed_bed{invalid_bed};
        REQUIRE_FALSE(
            fluids::make_shallow_water_reference_grid(
                valid_config,
                malformed_bed,
                state));
    }

    const std::array invalid_states{
        fluids::ShallowWaterConservedState{
            -1.0,
            0.0,
            0.0,
        },
        fluids::ShallowWaterConservedState{
            std::numeric_limits<double>::infinity(),
            0.0,
            0.0,
        },
        fluids::ShallowWaterConservedState{
            0.0,
            1.0,
            0.0,
        },
        fluids::ShallowWaterConservedState{
            std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::max(),
            0.0,
        },
        fluids::ShallowWaterConservedState{
            std::numeric_limits<double>::max(),
            0.0,
            0.0,
        },
    };
    for (const auto& invalid_state : invalid_states) {
        const std::array malformed_state{invalid_state};
        REQUIRE_FALSE(
            fluids::make_shallow_water_reference_grid(
                valid_config,
                bed,
                malformed_state));
    }

    constexpr std::array<double, 1> maximum_bed{
        std::numeric_limits<double>::max()};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        maximum_depth{{
            {
                std::numeric_limits<double>::max(),
                0.0,
                0.0,
            },
        }};
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            valid_config,
            maximum_bed,
            maximum_depth));

    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        overflowing_flux{{
            {
                1.0,
                std::numeric_limits<double>::max(),
                0.0,
            },
        }};
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            valid_config,
            bed,
            overflowing_flux));

    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        overflowing_integrated_momentum{{
            {
                1.0E100,
                1.0E109,
                0.0,
            },
        }};
    REQUIRE_FALSE(
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0E100,
            },
            bed,
            overflowing_integrated_momentum));
}

TEST_CASE(
    "shallow-water validation rejects corrupted active state and tails",
    "[fluids][shallow-water][reference][validation]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        state{{{1.0, 0.0, 0.0}}};
    const auto valid_result =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            state);
    REQUIRE(valid_result);
    const auto valid = valid_result.value();

    auto wrong_count = valid;
    wrong_count.cell_count = 0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            wrong_count));

    auto negative_zero = valid;
    negative_zero.states[0].momentum_x = -0.0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            negative_zero));

    auto negative_zero_x_origin = valid;
    negative_zero_x_origin.config.origin_x = -0.0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            negative_zero_x_origin));

    auto negative_zero_z_origin = valid;
    negative_zero_z_origin.config.origin_z = -0.0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            negative_zero_z_origin));

    auto nonfinite = valid;
    nonfinite.states[0].momentum_z =
        std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            nonfinite));

    auto stale_bed_tail = valid;
    stale_bed_tail.bed_elevations[1] = 1.0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            stale_bed_tail));

    auto stale_state_tail = valid;
    stale_state_tail.states[1].water_depth = 1.0;
    REQUIRE_FALSE(
        fluids::validate_shallow_water_reference_grid(
            stale_state_tail));
    REQUIRE_FALSE(
        fluids::inspect_shallow_water_reference_grid(
            stale_state_tail));
    REQUIRE_FALSE(
        fluids::sample_shallow_water_reference_interface(
            stale_state_tail,
            0,
            0,
            fluids::ShallowWaterFace::positive_x));
}

TEST_CASE(
    "shallow-water construction canonicalizes every signed zero",
    "[fluids][shallow-water][reference][contract][zero]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{-0.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        state{{{-0.0, -0.0, -0.0}}};
    const auto grid =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .origin_x = -0.0,
                .origin_z = -0.0,
                .cell_spacing = 1.0,
            },
            bed,
            state);
    REQUIRE(grid);
    REQUIRE(positive_zero(grid.value().config.origin_x));
    REQUIRE(positive_zero(grid.value().config.origin_z));
    REQUIRE(positive_zero(grid.value().bed_elevations[0]));
    REQUIRE(positive_zero(
        grid.value().states[0].water_depth));
    REQUIRE(positive_zero(
        grid.value().states[0].momentum_x));
    REQUIRE(positive_zero(
        grid.value().states[0].momentum_z));
    REQUIRE(fluids::validate_shallow_water_reference_grid(
        grid.value()));
}

TEST_CASE(
    "uneven canonical terrain produces a repeatable lake at rest",
    "[fluids][shallow-water][reference][lake-at-rest][terrain]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(
            make_uneven_canonical_tile());
    REQUIRE(surface_result);
    const auto surface =
        std::move(surface_result).value();
    const auto bed = canonical_cell_average_bed(surface);
    const auto& tile = surface.tile();
    const fluids::ShallowWaterGridConfig config{
        .columns = tile.sample_columns - 1U,
        .rows = tile.sample_rows - 1U,
        .origin_x = tile.origin.x,
        .origin_z = tile.origin.z,
        .cell_spacing = tile.sample_spacing,
    };
    constexpr auto waterline = 4.0;
    const auto active_count =
        static_cast<std::size_t>(config.columns) *
        config.rows;
    const auto active_bed = std::span<const double>{
        bed.data(),
        active_count};

    const auto first =
        fluids::make_lake_at_rest_reference_grid(
            config,
            active_bed,
            waterline);
    const auto second =
        fluids::make_lake_at_rest_reference_grid(
            config,
            active_bed,
            waterline);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.value() == second.value());
    const auto& lake = first.value();

    auto minimum_bed =
        std::numeric_limits<double>::infinity();
    auto maximum_bed =
        -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0;
         index < lake.cell_count;
         ++index) {
        minimum_bed =
            std::min(minimum_bed, lake.bed_elevations[index]);
        maximum_bed =
            std::max(maximum_bed, lake.bed_elevations[index]);
        REQUIRE(lake.states[index].water_depth > 0.0);
        REQUIRE(lake.states[index].momentum_x == 0.0);
        REQUIRE(lake.states[index].momentum_z == 0.0);
        REQUIRE(
            lake.bed_elevations[index] +
                lake.states[index].water_depth ==
            Catch::Approx(waterline)
                .epsilon(0.0)
                .margin(8.0 *
                    std::numeric_limits<double>::epsilon()));
    }
    REQUIRE(maximum_bed > minimum_bed);

    const auto diagnostics =
        fluids::inspect_shallow_water_reference_grid(lake);
    REQUIRE(diagnostics);
    REQUIRE(diagnostics.value().cell_count == 12);
    REQUIRE(diagnostics.value().wet_cell_count == 12);
    REQUIRE(diagnostics.value().dry_cell_count == 0);
    REQUIRE(
        diagnostics.value().
            minimum_free_surface_elevation ==
        Catch::Approx(waterline)
            .epsilon(0.0)
            .margin(8.0 *
                std::numeric_limits<double>::epsilon()));
    REQUIRE(
        diagnostics.value().
            maximum_free_surface_elevation ==
        Catch::Approx(waterline)
            .epsilon(0.0)
            .margin(8.0 *
                std::numeric_limits<double>::epsilon()));
    REQUIRE(
        diagnostics.value().
            maximum_absolute_momentum_x == 0.0);
    REQUIRE(
        diagnostics.value().
            maximum_absolute_momentum_z == 0.0);

    std::size_t solid_face_count = 0;
    for (std::uint32_t row = 0;
         row < config.rows;
         ++row) {
        for (std::uint32_t column = 0;
             column < config.columns;
             ++column) {
            for (const auto face : {
                    fluids::ShallowWaterFace::negative_x,
                    fluids::ShallowWaterFace::positive_x,
                    fluids::ShallowWaterFace::negative_z,
                    fluids::ShallowWaterFace::positive_z}) {
                const auto interface =
                    fluids::
                        sample_shallow_water_reference_interface(
                            lake,
                            column,
                            row,
                            face);
                REQUIRE(interface);
                if (!interface.value().
                        exterior_is_solid_ghost) {
                    continue;
                }
                ++solid_face_count;
                REQUIRE(interface.value().exterior ==
                    interface.value().interior);
            }
        }
    }
    REQUIRE(solid_face_count ==
        2U * config.columns + 2U * config.rows);
}

TEST_CASE(
    "lake-at-rest construction remains fully wet and finite",
    "[fluids][shallow-water][reference][lake-at-rest][validation]")
{
    using namespace shark;

    constexpr auto config =
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 1,
            .cell_spacing = 1.0,
        };
    constexpr std::array<double, 2> bed{-1.0, 2.0};

    REQUIRE_FALSE(
        fluids::make_lake_at_rest_reference_grid(
            config,
            bed,
            2.0));
    REQUIRE_FALSE(
        fluids::make_lake_at_rest_reference_grid(
            config,
            bed,
            1.0));
    REQUIRE_FALSE(
        fluids::make_lake_at_rest_reference_grid(
            config,
            bed,
            std::numeric_limits<double>::infinity()));

    constexpr std::array<double, 1> deep_bed{
        -std::numeric_limits<double>::max()};
    REQUIRE_FALSE(
        fluids::make_lake_at_rest_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            deep_bed,
            std::numeric_limits<double>::max()));

    const auto lake =
        fluids::make_lake_at_rest_reference_grid(
            config,
            bed,
            4.0);
    REQUIRE(lake);
    auto disturbed = lake.value();
    disturbed.states[0].water_depth =
        std::nextafter(
            disturbed.states[0].water_depth,
            std::numeric_limits<double>::infinity());
    disturbed.states[1].momentum_x =
        std::numeric_limits<double>::epsilon();
    const auto diagnostics =
        fluids::inspect_shallow_water_reference_grid(
            disturbed);
    REQUIRE(diagnostics);
    REQUIRE(
        diagnostics.value().
            maximum_free_surface_elevation >
        diagnostics.value().
            minimum_free_surface_elevation);
    REQUIRE(
        diagnostics.value().
            maximum_absolute_momentum_x >
        0.0);
}

TEST_CASE(
    "solid boundaries reflect only cardinal normal momentum",
    "[fluids][shallow-water][reference][boundary][solid]")
{
    using namespace shark;

    constexpr auto config =
        fluids::ShallowWaterGridConfig{
            .columns = 2,
            .rows = 2,
            .cell_spacing = 1.0,
        };
    constexpr std::array<double, 4> bed{
        10.0,
        20.0,
        30.0,
        40.0,
    };
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        4>
        states{{
            {1.0, 2.0, 3.0},
            {2.0, 4.0, 5.0},
            {3.0, 6.0, 7.0},
            {4.0, 8.0, 9.0},
        }};
    const auto grid_result =
        fluids::make_shallow_water_reference_grid(
            config,
            bed,
            states);
    REQUIRE(grid_result);
    const auto grid = grid_result.value();

    const auto interior =
        fluids::sample_shallow_water_reference_interface(
            grid,
            0,
            0,
            fluids::ShallowWaterFace::positive_x);
    REQUIRE(interior);
    REQUIRE_FALSE(
        interior.value().exterior_is_solid_ghost);
    REQUIRE(interior.value().interior ==
        fluids::ShallowWaterCell{bed[0], states[0]});
    REQUIRE(interior.value().exterior ==
        fluids::ShallowWaterCell{bed[1], states[1]});

    const auto west =
        fluids::sample_shallow_water_reference_interface(
            grid,
            0,
            0,
            fluids::ShallowWaterFace::negative_x);
    REQUIRE(west);
    REQUIRE(west.value().exterior_is_solid_ghost);
    REQUIRE(west.value().exterior.bed_elevation == bed[0]);
    REQUIRE(
        west.value().exterior.state.water_depth ==
        states[0].water_depth);
    REQUIRE(
        west.value().exterior.state.momentum_x ==
        -states[0].momentum_x);
    REQUIRE(
        west.value().exterior.state.momentum_z ==
        states[0].momentum_z);

    const auto east =
        fluids::sample_shallow_water_reference_interface(
            grid,
            1,
            1,
            fluids::ShallowWaterFace::positive_x);
    REQUIRE(east);
    REQUIRE(east.value().exterior.state.momentum_x ==
        -states[3].momentum_x);
    REQUIRE(east.value().exterior.state.momentum_z ==
        states[3].momentum_z);

    const auto south =
        fluids::sample_shallow_water_reference_interface(
            grid,
            0,
            0,
            fluids::ShallowWaterFace::negative_z);
    REQUIRE(south);
    REQUIRE(south.value().exterior.state.momentum_x ==
        states[0].momentum_x);
    REQUIRE(south.value().exterior.state.momentum_z ==
        -states[0].momentum_z);

    const auto north =
        fluids::sample_shallow_water_reference_interface(
            grid,
            1,
            1,
            fluids::ShallowWaterFace::positive_z);
    REQUIRE(north);
    REQUIRE(north.value().exterior.state.momentum_x ==
        states[3].momentum_x);
    REQUIRE(north.value().exterior.state.momentum_z ==
        -states[3].momentum_z);
    REQUIRE(grid == grid_result.value());

    REQUIRE_FALSE(
        fluids::sample_shallow_water_reference_interface(
            grid,
            2,
            0,
            fluids::ShallowWaterFace::negative_x));
    REQUIRE_FALSE(
        fluids::sample_shallow_water_reference_interface(
            grid,
            0,
            0,
            static_cast<fluids::ShallowWaterFace>(0)));
}

TEST_CASE(
    "one-cell solid domain reflects every face without negative zero",
    "[fluids][shallow-water][reference][boundary][solid]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{-2.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        state{{{3.0, 0.0, 0.0}}};
    const auto grid =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 1.0,
            },
            bed,
            state);
    REQUIRE(grid);

    for (const auto face : {
            fluids::ShallowWaterFace::negative_x,
            fluids::ShallowWaterFace::positive_x,
            fluids::ShallowWaterFace::negative_z,
            fluids::ShallowWaterFace::positive_z}) {
        const auto interface =
            fluids::sample_shallow_water_reference_interface(
                grid.value(),
                0,
                0,
                face);
        REQUIRE(interface);
        REQUIRE(interface.value().
            exterior_is_solid_ghost);
        REQUIRE(interface.value().exterior ==
            interface.value().interior);
        REQUIRE(positive_zero(
            interface.value().
                exterior.state.momentum_x));
        REQUIRE(positive_zero(
            interface.value().
                exterior.state.momentum_z));
    }
}

TEST_CASE(
    "shallow-water diagnostics publish deterministic volume baselines",
    "[fluids][shallow-water][reference][diagnostics][volume]")
{
    using namespace shark;

    constexpr std::array<double, 6> bed{
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
    };
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        6>
        states{{
            {1.0, 1.0, 0.5},
            {2.0, -2.0, 1.0},
            {0.0, 0.0, 0.0},
            {3.0, 3.0, -1.0},
            {4.0, -4.0, 2.0},
            {0.5, 0.5, -0.5},
        }};
    const auto grid =
        fluids::make_shallow_water_reference_grid(
            test_config(),
            bed,
            states);
    REQUIRE(grid);

    const auto first =
        fluids::inspect_shallow_water_reference_grid(
            grid.value());
    const auto second =
        fluids::inspect_shallow_water_reference_grid(
            grid.value());
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.value() == second.value());
    const auto& diagnostics = first.value();
    REQUIRE(diagnostics.cell_count == 6);
    REQUIRE(diagnostics.wet_cell_count == 5);
    REQUIRE(diagnostics.dry_cell_count == 1);
    REQUIRE(diagnostics.minimum_water_depth == 0.0);
    REQUIRE(diagnostics.maximum_water_depth == 4.0);
    REQUIRE(
        diagnostics.minimum_free_surface_elevation ==
        0.5);
    REQUIRE(
        diagnostics.maximum_free_surface_elevation ==
        4.0);
    REQUIRE(diagnostics.water_volume == 42.0);
    REQUIRE(diagnostics.integrated_momentum_x == -6.0);
    REQUIRE(diagnostics.integrated_momentum_z == 8.0);
    REQUIRE(
        diagnostics.maximum_absolute_momentum_x == 4.0);
    REQUIRE(
        diagnostics.maximum_absolute_momentum_z == 2.0);
}

TEST_CASE(
    "an exact dry shallow-water state has a zero inventory",
    "[fluids][shallow-water][reference][dry][state]")
{
    using namespace shark;

    constexpr std::array<double, 1> bed{12.0};
    constexpr std::array<
        fluids::ShallowWaterConservedState,
        1>
        dry{{{0.0, 0.0, 0.0}}};
    const auto grid =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns = 1,
                .rows = 1,
                .cell_spacing = 2.0,
            },
            bed,
            dry);
    REQUIRE(grid);
    const auto diagnostics =
        fluids::inspect_shallow_water_reference_grid(
            grid.value());
    REQUIRE(diagnostics);
    REQUIRE(diagnostics.value().wet_cell_count == 0);
    REQUIRE(diagnostics.value().dry_cell_count == 1);
    REQUIRE(diagnostics.value().water_volume == 0.0);
    REQUIRE(
        diagnostics.value().
            minimum_free_surface_elevation == 0.0);
    REQUIRE(
        diagnostics.value().
            maximum_free_surface_elevation == 0.0);
}

TEST_CASE(
    "shallow-water diagnostics reject aggregate finite-range overflow",
    "[fluids][shallow-water][reference][diagnostics][overflow]")
{
    using namespace shark;

    constexpr auto cell_count =
        fluids::shallow_water_reference_cell_capacity;
    std::array<double, cell_count> bed{};
    std::array<
        fluids::ShallowWaterConservedState,
        cell_count>
        states{};
    for (auto& state : states) {
        state.water_depth = 1.0E154;
    }
    const auto grid =
        fluids::make_shallow_water_reference_grid(
            fluids::ShallowWaterGridConfig{
                .columns =
                    fluids::
                        shallow_water_reference_max_columns,
                .rows =
                    fluids::shallow_water_reference_max_rows,
                .cell_spacing = 3.2E76,
            },
            bed,
            states);
    REQUIRE(grid);

    const auto diagnostics =
        fluids::inspect_shallow_water_reference_grid(
            grid.value());
    REQUIRE_FALSE(diagnostics);
    REQUIRE(diagnostics.error().code() ==
        core::ErrorCode::invalid_state);
}
