#include <shark/core/error.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>
#include <shark/world/island_demo_scenario.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::terrain::HeightTile flat_tile(
    const float bed_height)
{
    return {
        .sample_columns = 3,
        .sample_rows = 3,
        .sample_spacing = 2.0F,
        .origin = {-2.0F, bed_height, -2.0F},
        .height_offsets = std::vector<float>(9, 0.0F),
    };
}

[[nodiscard]] constexpr shark::water::CalmWaterBody test_body(
    const shark::water::CalmWaterSupportSide support_side =
        shark::water::CalmWaterSupportSide::
            inside_warped_footprint)
{
    return {
        .footprint = {
            .center_x = 0.0F,
            .center_z = 0.0F,
            .semi_axis_x = 2.0F,
            .semi_axis_z = 2.0F,
            .x_warp_square_offset = 0.0F,
            .x_warp_divisor = 16.0F,
            .z_warp_square_offset = 4.0F,
            .z_warp_divisor = 16.0F,
        },
        .support_side = support_side,
        .surface_height = 0.0F,
        .shoreline_depth_tolerance = 0.25F,
        .flow_velocity = shark::water::HorizontalFlow{},
    };
}

} // namespace

TEST_CASE(
    "gameplay water distinguishes horizontal support from terrain depth",
    "[water][gameplay][calm][support][depth]")
{
    using namespace shark;

    const auto surface_result =
        terrain::HeightTileSurface::create(flat_tile(-1.0F));
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();

    const auto inside_result = water::query_gameplay_water(
        test_body(),
        surface,
        0.0F,
        0.0F);
    REQUIRE(inside_result);
    const auto& inside = inside_result.value();
    REQUIRE(inside.disposition ==
        water::GameplayWaterDisposition::water);
    REQUIRE(inside.horizontal_support);
    REQUIRE(inside.surface_height == 0.0F);
    REQUIRE(inside.bed_height == -1.0F);
    REQUIRE(inside.depth == 1.0F);
    REQUIRE(inside.flow_velocity ==
        water::HorizontalFlow{});
    REQUIRE_FALSE(std::signbit(inside.flow_velocity->x));
    REQUIRE_FALSE(std::signbit(inside.flow_velocity->z));

    const auto outside_body = test_body(
        water::CalmWaterSupportSide::
            outside_warped_footprint);
    const auto unsupported_result = water::query_gameplay_water(
        outside_body,
        surface,
        0.0F,
        0.0F);
    REQUIRE(unsupported_result);
    const auto& unsupported = unsupported_result.value();
    REQUIRE(unsupported.disposition ==
        water::GameplayWaterDisposition::no_water);
    REQUIRE_FALSE(unsupported.horizontal_support);
    REQUIRE(unsupported.surface_height == 0.0F);
    REQUIRE(unsupported.bed_height == -1.0F);
    REQUIRE(unsupported.depth == 0.0F);
    REQUIRE_FALSE(std::signbit(unsupported.depth));
    REQUIRE_FALSE(unsupported.flow_velocity.has_value());

    // (2, 0) is exactly rho^2 == 1 for the locked footprint. Both support
    // sides mirror the shader's inclusive boundary classification.
    REQUIRE(
        terrain::island_normalized_radius_squared(
            test_body().footprint,
            2.0F,
            0.0F) == 1.0);
    for (const auto side : {
             water::CalmWaterSupportSide::
                 inside_warped_footprint,
             water::CalmWaterSupportSide::
                 outside_warped_footprint,
         }) {
        const auto boundary_result = water::query_gameplay_water(
            test_body(side),
            surface,
            2.0F,
            0.0F);
        REQUIRE(boundary_result);
        REQUIRE(boundary_result.value().disposition ==
            water::GameplayWaterDisposition::water);
        REQUIRE(boundary_result.value().horizontal_support);
        REQUIRE(boundary_result.value().depth == 1.0F);
    }

    const auto just_inside_x = std::nextafter(2.0F, 0.0F);
    const auto just_outside_x = std::nextafter(
        2.0F,
        std::numeric_limits<float>::infinity());
    auto neighbor_tile = flat_tile(-1.0F);
    neighbor_tile.sample_spacing = 4.0F;
    const auto neighbor_surface_result =
        terrain::HeightTileSurface::create(
            std::move(neighbor_tile));
    REQUIRE(neighbor_surface_result);
    const auto& neighbor_surface =
        neighbor_surface_result.value();
    REQUIRE(
        terrain::island_normalized_radius_squared(
            test_body().footprint,
            just_inside_x,
            0.0F) < 1.0);
    REQUIRE(
        terrain::island_normalized_radius_squared(
            test_body().footprint,
            just_outside_x,
            0.0F) > 1.0);
    struct SupportNeighborExpectation final {
        water::CalmWaterSupportSide side;
        water::GameplayWaterDisposition inside_neighbor;
        water::GameplayWaterDisposition outside_neighbor;
    };
    constexpr std::array<SupportNeighborExpectation, 2>
        neighbor_expectations{{
            {
                water::CalmWaterSupportSide::
                    inside_warped_footprint,
                water::GameplayWaterDisposition::water,
                water::GameplayWaterDisposition::no_water,
            },
            {
                water::CalmWaterSupportSide::
                    outside_warped_footprint,
                water::GameplayWaterDisposition::no_water,
                water::GameplayWaterDisposition::water,
            },
        }};
    for (const auto& expectation : neighbor_expectations) {
        const auto inside_neighbor =
            water::query_gameplay_water(
                test_body(expectation.side),
                neighbor_surface,
                just_inside_x,
                0.0F);
        const auto outside_neighbor =
            water::query_gameplay_water(
                test_body(expectation.side),
                neighbor_surface,
                just_outside_x,
                0.0F);
        REQUIRE(inside_neighbor);
        REQUIRE(outside_neighbor);
        REQUIRE(inside_neighbor.value().disposition ==
            expectation.inside_neighbor);
        REQUIRE(outside_neighbor.value().disposition ==
            expectation.outside_neighbor);
    }
}

TEST_CASE(
    "gameplay water canonicalizes flow and signed zero",
    "[water][gameplay][calm][flow][zero]")
{
    using namespace shark;

    auto signed_zero_tile = flat_tile(-0.0F);
    for (auto& offset : signed_zero_tile.height_offsets) {
        offset = -0.0F;
    }
    const auto zero_surface_result =
        terrain::HeightTileSurface::create(
            std::move(signed_zero_tile));
    REQUIRE(zero_surface_result);
    const auto raw_zero_sample =
        zero_surface_result.value().sample_lod0_height(
            0.0F,
            0.0F);
    REQUIRE(raw_zero_sample.has_value());
    REQUIRE(std::signbit(*raw_zero_sample));
    auto zero_body = test_body();
    zero_body.surface_height = -0.0F;
    zero_body.flow_velocity = water::HorizontalFlow{
        -0.0F,
        -0.0F,
    };
    const auto dry_result = water::query_gameplay_water(
        zero_body,
        zero_surface_result.value(),
        0.0F,
        0.0F);
    REQUIRE(dry_result);
    const auto& dry = dry_result.value();
    REQUIRE(dry.disposition ==
        water::GameplayWaterDisposition::no_water);
    REQUIRE_FALSE(std::signbit(dry.surface_height));
    REQUIRE_FALSE(std::signbit(dry.bed_height));
    REQUIRE_FALSE(std::signbit(dry.depth));
    REQUIRE_FALSE(dry.flow_velocity.has_value());

    const auto wet_surface_result =
        terrain::HeightTileSurface::create(flat_tile(-1.0F));
    REQUIRE(wet_surface_result);
    const auto& wet_surface = wet_surface_result.value();
    const auto zero_flow_result = water::query_gameplay_water(
        zero_body,
        wet_surface,
        0.0F,
        0.0F);
    REQUIRE(zero_flow_result);
    REQUIRE(zero_flow_result.value().disposition ==
        water::GameplayWaterDisposition::water);
    REQUIRE(zero_flow_result.value().flow_velocity.has_value());
    REQUIRE_FALSE(std::signbit(
        zero_flow_result.value().flow_velocity->x));
    REQUIRE_FALSE(std::signbit(
        zero_flow_result.value().flow_velocity->z));

    auto moving_body = test_body();
    constexpr water::HorizontalFlow expected_flow{
        1.5F,
        -2.0F,
    };
    moving_body.flow_velocity = expected_flow;
    const auto moving_result = water::query_gameplay_water(
        moving_body,
        wet_surface,
        0.0F,
        0.0F);
    REQUIRE(moving_result);
    REQUIRE(moving_result.value().flow_velocity ==
        expected_flow);

    moving_body.support_side =
        water::CalmWaterSupportSide::outside_warped_footprint;
    const auto unsupported_result = water::query_gameplay_water(
        moving_body,
        wet_surface,
        0.0F,
        0.0F);
    REQUIRE(unsupported_result);
    REQUIRE(unsupported_result.value().disposition ==
        water::GameplayWaterDisposition::no_water);
    REQUIRE_FALSE(
        unsupported_result.value().flow_velocity.has_value());

    const auto miss_result = water::query_gameplay_water(
        moving_body,
        wet_surface,
        3.0F,
        0.0F);
    REQUIRE(miss_result);
    REQUIRE(miss_result.value().disposition ==
        water::GameplayWaterDisposition::out_of_terrain);
    REQUIRE_FALSE(miss_result.value().flow_velocity.has_value());
}

TEST_CASE(
    "gameplay water reports representational range failures",
    "[water][gameplay][calm][range]")
{
    using namespace shark;

    const auto surface_result =
        terrain::HeightTileSurface::create(flat_tile(-1.0F));
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();

    auto tiny_body = test_body();
    tiny_body.footprint.semi_axis_x =
        std::numeric_limits<float>::denorm_min();
    tiny_body.footprint.semi_axis_z =
        std::numeric_limits<float>::denorm_min();
    tiny_body.footprint.x_warp_divisor =
        std::numeric_limits<float>::denorm_min();
    tiny_body.footprint.z_warp_divisor =
        std::numeric_limits<float>::denorm_min();

    // A finite terrain miss stays ordinary even when warped support would
    // exceed representable range. Terrain coverage is classified first.
    const auto extreme_miss = water::query_gameplay_water(
        tiny_body,
        surface,
        std::numeric_limits<float>::max(),
        0.0F);
    REQUIRE(extreme_miss);
    REQUIRE(extreme_miss.value().disposition ==
        water::GameplayWaterDisposition::out_of_terrain);
    REQUIRE_FALSE(extreme_miss.value().horizontal_support);

    tiny_body.footprint.center_x =
        -std::numeric_limits<float>::max();
    const auto support_overflow = water::query_gameplay_water(
        tiny_body,
        surface,
        0.0F,
        0.0F);
    REQUIRE_FALSE(support_overflow);
    REQUIRE(support_overflow.error().category() ==
        core::ErrorCategory::simulation);
    REQUIRE(support_overflow.error().code() ==
        core::ErrorCode::unavailable);

    const auto deep_surface_result =
        terrain::HeightTileSurface::create(
            flat_tile(-std::numeric_limits<float>::max()));
    REQUIRE(deep_surface_result);
    auto high_body = test_body();
    high_body.surface_height =
        std::numeric_limits<float>::max();
    const auto depth_overflow = water::query_gameplay_water(
        high_body,
        deep_surface_result.value(),
        0.0F,
        0.0F);
    REQUIRE_FALSE(depth_overflow);
    REQUIRE(depth_overflow.error().category() ==
        core::ErrorCategory::simulation);
    REQUIRE(depth_overflow.error().code() ==
        core::ErrorCode::unavailable);
}

TEST_CASE(
    "gameplay shoreline tolerance is strict and canonical",
    "[water][gameplay][calm][shoreline][tolerance]")
{
    using namespace shark;

    constexpr auto tolerance = 0.25F;
    const std::array<float, 3> bed_heights{
        std::nextafter(
            -tolerance,
            std::numeric_limits<float>::infinity()),
        -tolerance,
        std::nextafter(
            -tolerance,
            -std::numeric_limits<float>::infinity()),
    };
    const std::array<water::GameplayWaterDisposition, 3>
        expected_dispositions{
            water::GameplayWaterDisposition::no_water,
            water::GameplayWaterDisposition::no_water,
            water::GameplayWaterDisposition::water,
        };

    for (std::size_t index = 0;
         index < bed_heights.size();
         ++index) {
        const auto surface_result =
            terrain::HeightTileSurface::create(
                flat_tile(bed_heights[index]));
        REQUIRE(surface_result);
        const auto query_result = water::query_gameplay_water(
            test_body(),
            surface_result.value(),
            0.0F,
            0.0F);
        REQUIRE(query_result);
        const auto& query = query_result.value();
        REQUIRE(query.disposition ==
            expected_dispositions[index]);
        REQUIRE(query.horizontal_support);
        REQUIRE(query.bed_height == bed_heights[index]);
        if (query.disposition ==
            water::GameplayWaterDisposition::water) {
            REQUIRE(query.depth > tolerance);
            REQUIRE(query.depth ==
                0.0F - bed_heights[index]);
            REQUIRE(query.flow_velocity.has_value());
        }
        else {
            REQUIRE(query.depth == 0.0F);
            REQUIRE_FALSE(std::signbit(query.depth));
            REQUIRE_FALSE(query.flow_velocity.has_value());
        }
    }

    for (const auto bed_height : {0.0F, 1.0F}) {
        const auto surface_result =
            terrain::HeightTileSurface::create(
                flat_tile(bed_height));
        REQUIRE(surface_result);
        const auto query_result = water::query_gameplay_water(
            test_body(),
            surface_result.value(),
            0.0F,
            0.0F);
        REQUIRE(query_result);
        REQUIRE(query_result.value().disposition ==
            water::GameplayWaterDisposition::no_water);
        REQUIRE(query_result.value().depth == 0.0F);
    }
}

TEST_CASE(
    "gameplay water keeps terrain misses distinct from invalid input",
    "[water][gameplay][calm][terrain][validation]")
{
    using namespace shark;

    const auto surface_result =
        terrain::HeightTileSurface::create(flat_tile(-1.0F));
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    const auto body = test_body();

    for (const auto point : {
             std::array<float, 2>{-2.0F, 0.0F},
             std::array<float, 2>{2.0F, 0.0F},
             std::array<float, 2>{0.0F, -2.0F},
             std::array<float, 2>{0.0F, 2.0F},
         }) {
        const auto edge_result = water::query_gameplay_water(
            body,
            surface,
            point[0],
            point[1]);
        REQUIRE(edge_result);
        REQUIRE(edge_result.value().disposition !=
            water::GameplayWaterDisposition::out_of_terrain);
    }

    for (const auto point : {
             std::array<float, 2>{
                 std::nextafter(
                     -2.0F,
                     -std::numeric_limits<float>::infinity()),
                 0.0F,
             },
             std::array<float, 2>{
                 std::nextafter(
                     2.0F,
                     std::numeric_limits<float>::infinity()),
                 0.0F,
             },
             std::array<float, 2>{
                 0.0F,
                 std::nextafter(
                     -2.0F,
                     -std::numeric_limits<float>::infinity()),
             },
             std::array<float, 2>{
                 0.0F,
                 std::nextafter(
                     2.0F,
                     std::numeric_limits<float>::infinity()),
             },
         }) {
        const auto miss_result = water::query_gameplay_water(
            body,
            surface,
            point[0],
            point[1]);
        REQUIRE(miss_result);
        const auto& miss = miss_result.value();
        REQUIRE(miss.disposition ==
            water::GameplayWaterDisposition::out_of_terrain);
        REQUIRE_FALSE(miss.horizontal_support);
        REQUIRE(miss.surface_height == 0.0F);
        REQUIRE(miss.bed_height == 0.0F);
        REQUIRE(miss.depth == 0.0F);
        REQUIRE_FALSE(std::signbit(miss.bed_height));
        REQUIRE_FALSE(std::signbit(miss.depth));
        REQUIRE_FALSE(miss.flow_velocity.has_value());
    }

    for (const auto point : {
             std::array<float, 2>{
                 std::numeric_limits<float>::quiet_NaN(),
                 0.0F,
             },
             std::array<float, 2>{
                 0.0F,
                 std::numeric_limits<float>::infinity(),
             },
         }) {
        const auto invalid_result = water::query_gameplay_water(
            body,
            surface,
            point[0],
            point[1]);
        REQUIRE_FALSE(invalid_result);
        REQUIRE(invalid_result.error().category() ==
            core::ErrorCategory::simulation);
        REQUIRE(invalid_result.error().code() ==
            core::ErrorCode::invalid_argument);
    }
}

TEST_CASE(
    "gameplay water rejects malformed authored bodies transactionally",
    "[water][gameplay][calm][validation][transaction]")
{
    using namespace shark;

    const auto surface_result =
        terrain::HeightTileSurface::create(flat_tile(-1.0F));
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    const auto original_tile = surface.tile();
    const auto original_body = test_body();

    std::vector<water::CalmWaterBody> invalid_bodies;
    auto invalid = original_body;
    invalid.footprint.center_x =
        std::numeric_limits<float>::quiet_NaN();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.center_z =
        std::numeric_limits<float>::infinity();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.semi_axis_x = 0.0F;
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.semi_axis_z = -1.0F;
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.x_warp_square_offset =
        std::numeric_limits<float>::quiet_NaN();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.z_warp_square_offset =
        std::numeric_limits<float>::infinity();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.x_warp_divisor = 0.0F;
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.footprint.z_warp_divisor = -1.0F;
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.support_side =
        static_cast<water::CalmWaterSupportSide>(0);
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.surface_height =
        std::numeric_limits<float>::quiet_NaN();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.shoreline_depth_tolerance = 0.0F;
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.shoreline_depth_tolerance =
        std::numeric_limits<float>::infinity();
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.flow_velocity =
        water::HorizontalFlow{
            std::numeric_limits<float>::quiet_NaN(),
            0.0F,
        };
    invalid_bodies.push_back(invalid);
    invalid = original_body;
    invalid.flow_velocity =
        water::HorizontalFlow{
            0.0F,
            std::numeric_limits<float>::infinity(),
        };
    invalid_bodies.push_back(invalid);

    for (const auto& invalid_body : invalid_bodies) {
        const auto query_result = water::query_gameplay_water(
            invalid_body,
            surface,
            0.0F,
            0.0F);
        REQUIRE_FALSE(query_result);
        REQUIRE(query_result.error().category() ==
            core::ErrorCategory::simulation);
        REQUIRE(query_result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(surface.tile() == original_tile);
    }
    REQUIRE(original_body == test_body());

    auto absent_flow = original_body;
    absent_flow.flow_velocity = std::nullopt;
    const auto absent_result = water::query_gameplay_water(
        absent_flow,
        surface,
        0.0F,
        0.0F);
    REQUIRE(absent_result);
    REQUIRE(absent_result.value().disposition ==
        water::GameplayWaterDisposition::water);
    REQUIRE_FALSE(
        absent_result.value().flow_velocity.has_value());
}

TEST_CASE(
    "Island Demo gameplay water matches its authored shore transect",
    "[water][gameplay][calm][island-demo][integration]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto surface_result =
        terrain::HeightTileSurface::create(scenario.terrain);
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    const auto& body = scenario.water.gameplay_body;

    REQUIRE(body.footprint == scenario.island.footprint);
    REQUIRE(body.support_side ==
        water::CalmWaterSupportSide::
            outside_warped_footprint);
    REQUIRE(body.surface_height ==
        scenario.island.waterline_y);
    REQUIRE(body.shoreline_depth_tolerance ==
        water::default_shoreline_depth_tolerance);
    REQUIRE(body.flow_velocity ==
        water::HorizontalFlow{});

    constexpr std::array<float, 4> expected_depths{
        0.0F,
        0.33984375F,
        1.359375F,
        5.734375F,
    };
    for (std::size_t index = 0;
         index < scenario.shore_entry_samples.size();
         ++index) {
        const auto& point =
            scenario.shore_entry_samples[index];
        const auto query_result = water::query_gameplay_water(
            body,
            surface,
            point.x,
            point.z);
        REQUIRE(query_result);
        const auto& query = query_result.value();
        REQUIRE(query.disposition ==
            (index == 0U
                 ? water::GameplayWaterDisposition::no_water
                 : water::GameplayWaterDisposition::water));
        REQUIRE(query.surface_height ==
            body.surface_height);
        REQUIRE(query.bed_height == point.y);
        REQUIRE(query.depth == expected_depths[index]);
        REQUIRE(query.horizontal_support == (index > 0U));
        REQUIRE(query.flow_velocity.has_value() ==
            (index > 0U));
        if (query.flow_velocity.has_value()) {
            REQUIRE(*query.flow_velocity ==
                water::HorizontalFlow{});
        }
        for (std::size_t repeat = 0; repeat < 8U; ++repeat) {
            const auto repeated = water::query_gameplay_water(
                body,
                surface,
                point.x,
                point.z);
            REQUIRE(repeated);
            REQUIRE(repeated.value() == query);
        }
    }

    for (const auto point : {
             std::array<float, 2>{
                 surface.bounds().minimum.x,
                 surface.bounds().minimum.z,
             },
             std::array<float, 2>{
                 surface.bounds().maximum.x,
                 surface.bounds().maximum.z,
             },
         }) {
        const auto edge_result = water::query_gameplay_water(
            body,
            surface,
            point[0],
            point[1]);
        REQUIRE(edge_result);
        REQUIRE(edge_result.value().disposition ==
            water::GameplayWaterDisposition::water);
        REQUIRE(edge_result.value().depth >= 7.0F);
    }

    for (const auto point : {
             std::array<float, 2>{
                 std::nextafter(
                     surface.bounds().minimum.x,
                     -std::numeric_limits<float>::infinity()),
                 0.0F,
             },
             std::array<float, 2>{
                 std::nextafter(
                     surface.bounds().maximum.x,
                     std::numeric_limits<float>::infinity()),
                 0.0F,
             },
         }) {
        const auto outside_result =
            water::query_gameplay_water(
                body,
                surface,
                point[0],
                point[1]);
        REQUIRE(outside_result);
        REQUIRE(outside_result.value().disposition ==
            water::GameplayWaterDisposition::out_of_terrain);
    }
}

TEST_CASE(
    "Island Demo shoreline changes only after canonical depth tolerance",
    "[water][gameplay][calm][island-demo][shoreline][adjacent]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto surface_result =
        terrain::HeightTileSurface::create(scenario.terrain);
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    const auto& body = scenario.water.gameplay_body;

    auto no_water_z = 160.0F;
    auto water_z = 176.0F;
    while (std::nextafter(
               no_water_z,
               std::numeric_limits<float>::infinity()) <
           water_z) {
        const auto midpoint =
            no_water_z + (water_z - no_water_z) * 0.5F;
        if (midpoint == no_water_z || midpoint == water_z) {
            break;
        }
        const auto query_result = water::query_gameplay_water(
            body,
            surface,
            0.0F,
            midpoint);
        REQUIRE(query_result);
        if (query_result.value().disposition ==
            water::GameplayWaterDisposition::water) {
            water_z = midpoint;
        }
        else {
            no_water_z = midpoint;
        }
    }

    REQUIRE(std::nextafter(
        no_water_z,
        std::numeric_limits<float>::infinity()) == water_z);
    const auto no_water_result = water::query_gameplay_water(
        body,
        surface,
        0.0F,
        no_water_z);
    const auto water_result = water::query_gameplay_water(
        body,
        surface,
        0.0F,
        water_z);
    REQUIRE(no_water_result);
    REQUIRE(water_result);
    REQUIRE(no_water_result.value().disposition ==
        water::GameplayWaterDisposition::no_water);
    REQUIRE(water_result.value().disposition ==
        water::GameplayWaterDisposition::water);
    REQUIRE(no_water_result.value().horizontal_support);
    REQUIRE(water_result.value().horizontal_support);

    const auto no_water_bed =
        surface.sample_lod0_height(0.0F, no_water_z);
    const auto water_bed =
        surface.sample_lod0_height(0.0F, water_z);
    REQUIRE(no_water_bed);
    REQUIRE(water_bed);
    REQUIRE(
        static_cast<double>(body.surface_height) -
            static_cast<double>(*no_water_bed) <=
        static_cast<double>(
            body.shoreline_depth_tolerance));
    REQUIRE(
        static_cast<double>(body.surface_height) -
            static_cast<double>(*water_bed) >
        static_cast<double>(
            body.shoreline_depth_tolerance));
    REQUIRE(water_result.value().depth ==
        body.surface_height - *water_bed);
}
