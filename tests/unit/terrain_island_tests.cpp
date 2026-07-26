#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/island.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

[[nodiscard]] constexpr shark::terrain::IslandShape test_shape() noexcept
{
    return shark::terrain::IslandShape{
        .footprint = {
            .center_x = 0.0F,
            .center_z = 0.0F,
            .semi_axis_x = 210.0F,
            .semi_axis_z = 170.0F,
            .x_warp_square_offset = 0.0F,
            .x_warp_divisor = 4'096.0F,
            .z_warp_square_offset = 0.0F,
            .z_warp_divisor = 4'096.0F,
        },
        .waterline_y = -4.0F,
        .shoreline_land_clearance = 0.75F,
        .interior_land_height = 8.0F,
        .natural_relief_scale = 0.25F,
        .shoreline_water_depth = 0.25F,
        .deep_water_depth = 9.0F,
        .deep_water_end_radius_squared = 2.25,
    };
}

[[nodiscard]] std::size_t sample_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(z) * columns + x;
}

} // namespace

TEST_CASE(
    "playable island shaping is deterministic and preserves topology",
    "[terrain][island][determinism][contract]")
{
    using namespace shark;

    const auto base = terrain::make_large_capacity_height_tile();
    const auto first_result =
        terrain::shape_playable_island(base, test_shape());
    const auto second_result =
        terrain::shape_playable_island(base, test_shape());
    REQUIRE(first_result);
    REQUIRE(second_result);
    const auto& first = first_result.value();
    const auto& second = second_result.value();
    REQUIRE(first == second);
    REQUIRE(base == terrain::make_large_capacity_height_tile());
    REQUIRE(first.sample_columns == base.sample_columns);
    REQUIRE(first.sample_rows == base.sample_rows);
    REQUIRE(first.sample_spacing == base.sample_spacing);
    REQUIRE(first.origin == base.origin);
    REQUIRE(first.height_offsets.size() ==
        terrain::large_capacity_tile_vertex_count);
    REQUIRE(std::all_of(
        first.height_offsets.begin(),
        first.height_offsets.end(),
        [](const float height) {
            return std::isfinite(height) &&
                height * 256.0F ==
                    std::trunc(height * 256.0F);
        }));

    std::size_t dry_samples = 0;
    std::size_t shallow_samples = 0;
    std::size_t deep_samples = 0;
    bool classification_agrees = true;
    bool dry_perimeter = false;
    for (std::uint32_t z = 0; z < first.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < first.sample_columns; ++x) {
            const auto index = sample_index(
                x,
                z,
                first.sample_columns);
            const auto world_x =
                first.origin.x +
                static_cast<float>(x) * first.sample_spacing;
            const auto world_z =
                first.origin.z +
                static_cast<float>(z) * first.sample_spacing;
            const auto world_height =
                first.origin.y + first.height_offsets[index];
            const auto radius_squared =
                terrain::island_normalized_radius_squared(
                    test_shape().footprint,
                    world_x,
                    world_z);
            const auto expected_dry = radius_squared < 1.0;
            const auto is_dry =
                world_height > test_shape().waterline_y;
            classification_agrees =
                classification_agrees &&
                expected_dry == is_dry;
            if (is_dry) {
                ++dry_samples;
                dry_perimeter =
                    dry_perimeter ||
                    x == 0U ||
                    z == 0U ||
                    x + 1U == first.sample_columns ||
                    z + 1U == first.sample_rows;
            }
            else {
                const auto depth =
                    test_shape().waterline_y - world_height;
                shallow_samples += depth <= 2.0F ? 1U : 0U;
                deep_samples += depth >= 6.75F ? 1U : 0U;
            }
        }
    }
    REQUIRE(classification_agrees);
    REQUIRE_FALSE(dry_perimeter);
    REQUIRE(dry_samples > 4'000U);
    REQUIRE(dry_samples < 12'000U);
    REQUIRE(shallow_samples > 500U);
    REQUIRE(deep_samples > 30'000U);
}

TEST_CASE(
    "playable island shaping rejects invalid public contracts",
    "[terrain][island][validation]")
{
    using namespace shark;

    REQUIRE(
        terrain::island_normalized_radius_squared(
            test_shape().footprint,
            0.0F,
            0.0F) == 0.0);
    REQUIRE(std::isinf(
        terrain::island_normalized_radius_squared(
            {},
            0.0F,
            0.0F)));
    REQUIRE(std::isinf(
        terrain::island_normalized_radius_squared(
            test_shape().footprint,
            std::numeric_limits<float>::infinity(),
            0.0F)));

    REQUIRE_FALSE(terrain::shape_playable_island({}, test_shape()));

    auto invalid = test_shape();
    invalid.footprint.semi_axis_x = 0.0F;
    REQUIRE_FALSE(terrain::shape_playable_island(
        terrain::make_large_capacity_height_tile(),
        invalid));
    invalid = test_shape();
    invalid.natural_relief_scale = 1.25F;
    REQUIRE_FALSE(terrain::shape_playable_island(
        terrain::make_large_capacity_height_tile(),
        invalid));
    invalid = test_shape();
    invalid.deep_water_end_radius_squared = 1.0;
    REQUIRE_FALSE(terrain::shape_playable_island(
        terrain::make_large_capacity_height_tile(),
        invalid));
}
