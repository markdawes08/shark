#include <catch2/catch_test_macros.hpp>

#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/lake_basin.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] constexpr shark::terrain::LakeBasinShape test_shape()
{
    return shark::terrain::LakeBasinShape{
        .footprint = {
            .center = {-128.0F, -128.0F},
            .semi_axis_x = 56.0F,
            .semi_axis_z = 48.0F,
            .x_warp_square_offset = 1'152.0F,
            .x_warp_divisor = 512.0F,
            .z_warp_square_offset = 1'568.0F,
            .z_warp_divisor = 1'024.0F,
        },
        .future_waterline_y = -4.0F,
        .core_depth = 6.5F,
        .rim_height = 1.5F,
        .rise_end_radius = 23.0 / 20.0,
        .rim_end_radius = 27.0 / 20.0,
        .blend_end_radius = 2.0,
    };
}

[[nodiscard]] std::uint64_t height_checksum(
    const std::vector<float>& heights) noexcept
{
    constexpr std::uint64_t fnv_offset_basis =
        0xCBF2'9CE4'8422'2325ULL;
    constexpr std::uint64_t fnv_prime = 0x0000'0100'0000'01B3ULL;
    auto result = fnv_offset_basis;
    for (const auto height : heights) {
        const auto bits = std::bit_cast<std::uint32_t>(height);
        for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
            result ^= (bits >> shift) & 0xFFU;
            result *= fnv_prime;
        }
    }
    return result;
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
    "closed lake-basin shaping preserves topology and locks its Q8 fixture",
    "[terrain][lake-basin][contract]")
{
    using namespace shark;

    const auto base = terrain::make_large_capacity_height_tile();
    const auto shaped_result = terrain::shape_closed_lake_basin(
        base,
        test_shape());
    REQUIRE(shaped_result);
    const auto& shaped = shaped_result.value();
    REQUIRE(shaped.sample_columns == base.sample_columns);
    REQUIRE(shaped.sample_rows == base.sample_rows);
    REQUIRE(shaped.sample_spacing == base.sample_spacing);
    REQUIRE(shaped.origin == base.origin);
    REQUIRE(shaped.height_offsets.size() ==
        base.height_offsets.size());
    REQUIRE(height_checksum(shaped.height_offsets) ==
        0x4890'DE3E'1AA0'63A9ULL);

    std::size_t footprint_samples = 0;
    std::size_t rim_samples = 0;
    std::size_t unchanged_samples = 0;
    const auto shape = test_shape();
    const auto rise_end_squared =
        shape.rise_end_radius * shape.rise_end_radius;
    const auto rim_end_squared =
        shape.rim_end_radius * shape.rim_end_radius;
    const auto blend_end_squared =
        shape.blend_end_radius * shape.blend_end_radius;
    for (std::uint32_t z = 0; z < shaped.sample_rows; ++z) {
        const auto world_z =
            shaped.origin.z +
            static_cast<float>(z) * shaped.sample_spacing;
        for (std::uint32_t x = 0;
             x < shaped.sample_columns;
             ++x) {
            const auto index =
                sample_index(x, z, shaped.sample_columns);
            const auto world_x =
                shaped.origin.x +
                static_cast<float>(x) *
                    shaped.sample_spacing;
            const auto field =
                terrain::lake_basin_normalized_radius_squared(
                    shape.footprint,
                    {world_x, world_z});
            if (field <= 1.0) {
                ++footprint_samples;
                REQUIRE(
                    shaped.origin.y +
                        shaped.height_offsets[index] <=
                    shape.future_waterline_y);
            }
            if (field > rise_end_squared &&
                field <= rim_end_squared) {
                ++rim_samples;
                REQUIRE(
                    shaped.origin.y +
                        shaped.height_offsets[index] ==
                    shape.future_waterline_y +
                        shape.rim_height);
            }
            if (field > blend_end_squared) {
                ++unchanged_samples;
                REQUIRE(shaped.height_offsets[index] ==
                    base.height_offsets[index]);
            }
        }
    }
    REQUIRE(footprint_samples == 530);
    REQUIRE(rim_samples == 260);
    REQUIRE(unchanged_samples > 50'000);

    const auto surface_result =
        terrain::HeightTileSurface::create(shaped);
    REQUIRE(surface_result);
    const auto core_height =
        surface_result.value().sample_lod0_height(
            -124.0F,
            -128.0F);
    REQUIRE(core_height);
    REQUIRE(*core_height == -10.47265625F);
    REQUIRE(*core_height <=
        shape.future_waterline_y - 6.0F);

    const auto [minimum, maximum] = std::minmax_element(
        shaped.height_offsets.begin(),
        shaped.height_offsets.end());
    REQUIRE(*minimum ==
        terrain::large_capacity_tile_minimum_height_offset);
    REQUIRE(*maximum ==
        terrain::large_capacity_tile_maximum_height_offset);
}

TEST_CASE(
    "lake-basin shaping rejects malformed state and edge-reaching support",
    "[terrain][lake-basin][validation]")
{
    using namespace shark;

    const auto base = terrain::make_large_capacity_height_tile();

    SECTION("malformed tile") {
        auto malformed = base;
        malformed.height_offsets.pop_back();
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(
                std::move(malformed),
                test_shape()));
    }

    SECTION("non-Q8 tile spacing") {
        auto malformed = base;
        malformed.sample_spacing = 0.1F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(
                std::move(malformed),
                test_shape()));
    }

    SECTION("non-Q8 tile origin") {
        auto malformed = base;
        malformed.origin.y = -2.1F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(
                std::move(malformed),
                test_shape()));
    }

    SECTION("non-Q8 height sample") {
        auto malformed = base;
        malformed.height_offsets.front() += 0.001F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(
                std::move(malformed),
                test_shape()));
    }

    SECTION("nonfinite footprint") {
        auto shape = test_shape();
        shape.footprint.center.x =
            std::numeric_limits<float>::infinity();
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("invalid profile order") {
        auto shape = test_shape();
        shape.rim_end_radius = shape.rise_end_radius;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("non-Q8 waterline") {
        auto shape = test_shape();
        shape.future_waterline_y = -4.1F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("sub-Q8 axis") {
        auto shape = test_shape();
        shape.footprint.semi_axis_x = 0.001F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("sub-Q8 warp divisor") {
        auto shape = test_shape();
        shape.footprint.x_warp_divisor = 0.001F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("sub-Q8 core depth") {
        auto shape = test_shape();
        shape.core_depth = 0.001F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("sub-Q8 rim height") {
        auto shape = test_shape();
        shape.rim_height = 0.001F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("huge finite footprint") {
        auto shape = test_shape();
        shape.footprint.center.x =
            std::numeric_limits<float>::max();
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("unsafe finite warp") {
        auto shape = test_shape();
        shape.footprint.x_warp_divisor = 1.0F / 256.0F;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }

    SECTION("sampled rim has canonical escape gaps") {
        terrain::HeightTile low_exterior{
            .sample_columns = 33,
            .sample_rows = 33,
            .sample_spacing = 1.0F,
            .origin = {-16.0F, 0.0F, -16.0F},
            .height_offsets =
                std::vector<float>(33U * 33U, -3.0F),
        };
        auto shape = test_shape();
        shape.footprint = {
            .center = {0.0F, 0.0F},
            .semi_axis_x = 2.0F,
            .semi_axis_z = 2.0F,
            .x_warp_square_offset = 0.0F,
            .x_warp_divisor = 4'096.0F,
            .z_warp_square_offset = 0.0F,
            .z_warp_divisor = 4'096.0F,
        };
        shape.future_waterline_y = -4.0F;
        shape.core_depth = 1.0F;
        shape.rim_height = 1.5F;
        shape.rise_end_radius = 1.01;
        shape.rim_end_radius = 1.2;
        shape.blend_end_radius = 2.0;
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(
                std::move(low_exterior),
                shape));
    }

    SECTION("support reaches the tile boundary") {
        auto shape = test_shape();
        shape.footprint.center = {-470.0F, -470.0F};
        REQUIRE_FALSE(
            terrain::shape_closed_lake_basin(base, shape));
    }
}

TEST_CASE(
    "lake footprint evaluation is finite explicit and bounded",
    "[terrain][lake-basin][footprint]")
{
    using namespace shark;

    const auto shape = test_shape();
    const auto at_core =
        terrain::lake_basin_normalized_radius_squared(
            shape.footprint,
            {-124.0F, -128.0F});
    const auto at_spawn =
        terrain::lake_basin_normalized_radius_squared(
            shape.footprint,
            {-128.0F, -20.0F});
    REQUIRE(std::isfinite(at_core));
    REQUIRE(std::isfinite(at_spawn));
    REQUIRE(at_core < 0.01);
    REQUIRE(at_spawn > 1.0);

    auto invalid = shape.footprint;
    invalid.semi_axis_x = 0.0F;
    REQUIRE(std::isinf(
        terrain::lake_basin_normalized_radius_squared(
            invalid,
            {})));
}
