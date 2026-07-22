#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/box_contact.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr float comparison_margin = 0.00001F;

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

[[nodiscard]] float dot(
    const shark::math::Float3 first,
    const shark::math::Float3 second)
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

[[nodiscard]] shark::math::Float3 subtract(
    const shark::math::Float3 first,
    const shark::math::Float3 second)
{
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z,
    };
}

[[nodiscard]] bool approximately_equal(
    const shark::math::Float3 first,
    const shark::math::Float3 second,
    const float margin = comparison_margin)
{
    return std::abs(first.x - second.x) <= margin &&
        std::abs(first.y - second.y) <= margin &&
        std::abs(first.z - second.z) <= margin;
}

[[nodiscard]] shark::math::Float3 cross(
    const shark::math::Float3 first,
    const shark::math::Float3 second)
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x,
    };
}

[[nodiscard]] shark::math::Float3 normalized(
    const shark::math::Float3 value)
{
    const auto length = std::sqrt(dot(value, value));
    REQUIRE(length > 0.0F);
    return {
        value.x / length,
        value.y / length,
        value.z / length,
    };
}

void require_unit_vector(
    const shark::math::Float3 value,
    const float margin = comparison_margin)
{
    REQUIRE(dot(value, value) == Catch::Approx(1.0F).margin(margin));
}

[[nodiscard]] shark::physics::RigidBodyState body_at(
    const shark::math::Float3 position,
    const shark::math::Quaternion orientation = {})
{
    return shark::physics::RigidBodyState{
        .position = position,
        .orientation = orientation,
    };
}

[[nodiscard]] shark::physics::BoxCollider box(
    const shark::math::Float3 half_extents = {1.0F, 1.0F, 1.0F})
{
    return shark::physics::BoxCollider{
        .local_half_extents = half_extents,
    };
}

[[nodiscard]] shark::math::Quaternion axis_angle(
    const shark::math::Float3 unit_axis,
    const float angle)
{
    const auto sine = std::sin(angle * 0.5F);
    return {
        unit_axis.x * sine,
        unit_axis.y * sine,
        unit_axis.z * sine,
        std::cos(angle * 0.5F),
    };
}

void require_box_manifold_consistent(
    const shark::physics::BoxBoxContactManifold& manifold,
    const float margin = comparison_margin)
{
    REQUIRE(manifold.point_count > 0U);
    REQUIRE(
        manifold.point_count <=
        shark::physics::box_contact_manifold_capacity);
    require_unit_vector(manifold.normal, margin);
    REQUIRE(manifold.separation <= comparison_margin);
    REQUIRE(
        manifold.penetration_depth ==
        Catch::Approx(std::max(0.0F, -manifold.separation))
            .margin(margin));
    for (std::size_t index = 0U;
         index < manifold.point_count;
         ++index) {
        CAPTURE(index);
        const auto& point = manifold.points[index];
        const auto witness_separation = dot(
            subtract(point.point_on_second, point.point_on_first),
            manifold.normal);
        REQUIRE(
            point.separation ==
            Catch::Approx(witness_separation).margin(margin));
        REQUIRE(
            point.penetration_depth ==
            Catch::Approx(std::max(0.0F, -point.separation))
                .margin(margin));
        require_float3(
            point.position,
            {
                (point.point_on_first.x + point.point_on_second.x) * 0.5F,
                (point.point_on_first.y + point.point_on_second.y) * 0.5F,
                (point.point_on_first.z + point.point_on_second.z) * 0.5F,
            },
            margin);
    }
}

void require_terrain_manifold_consistent(
    const shark::physics::BoxTerrainContactManifold& manifold,
    const float margin = comparison_margin)
{
    REQUIRE(manifold.point_count > 0U);
    REQUIRE(
        manifold.point_count <=
        shark::physics::box_contact_manifold_capacity);
    require_unit_vector(manifold.normal, margin);
    REQUIRE(manifold.separation <= comparison_margin);
    REQUIRE(
        manifold.penetration_depth ==
        Catch::Approx(std::max(0.0F, -manifold.separation))
            .margin(margin));
    auto minimum_separation = std::numeric_limits<float>::max();
    for (std::size_t index = 0U;
         index < manifold.point_count;
         ++index) {
        CAPTURE(index);
        const auto& point = manifold.points[index];
        const auto witness_separation = dot(
            subtract(point.box_point, point.surface.position),
            manifold.normal);
        REQUIRE(
            point.separation ==
            Catch::Approx(witness_separation).margin(margin));
        REQUIRE(
            point.penetration_depth ==
            Catch::Approx(std::max(0.0F, -point.separation))
                .margin(margin));
        require_float3(
            point.position,
            {
                (point.box_point.x + point.surface.position.x) * 0.5F,
                (point.box_point.y + point.surface.position.y) * 0.5F,
                (point.box_point.z + point.surface.position.z) * 0.5F,
            },
            margin);
        REQUIRE(point.surface.normal.y > 0.0F);
        REQUIRE(
            dot(manifold.normal, point.surface.normal) >=
            -margin);
        const auto barycentric_sum =
            point.surface.barycentrics.x +
            point.surface.barycentrics.y +
            point.surface.barycentrics.z;
        REQUIRE(
            barycentric_sum ==
            Catch::Approx(1.0F).margin(margin));
        minimum_separation = std::min(
            minimum_separation,
            point.separation);
    }
    REQUIRE(
        manifold.separation ==
        Catch::Approx(minimum_separation).margin(margin));
}

[[nodiscard]] float projection_separation(
    const shark::physics::BoxWorldGeometry& first,
    const shark::physics::BoxWorldGeometry& second,
    const shark::math::Float3 candidate)
{
    const auto axis = normalized(candidate);
    auto first_minimum = dot(first.vertices[0], axis);
    auto first_maximum = first_minimum;
    auto second_minimum = dot(second.vertices[0], axis);
    auto second_maximum = second_minimum;
    for (std::size_t index = 1U;
         index < first.vertices.size();
         ++index) {
        const auto first_projection = dot(first.vertices[index], axis);
        const auto second_projection = dot(second.vertices[index], axis);
        first_minimum = std::min(first_minimum, first_projection);
        first_maximum = std::max(first_maximum, first_projection);
        second_minimum = std::min(second_minimum, second_projection);
        second_maximum = std::max(second_maximum, second_projection);
    }
    return std::max(
        second_minimum - first_maximum,
        first_minimum - second_maximum);
}

[[nodiscard]] shark::terrain::HeightTile make_tile(
    const std::uint32_t columns,
    const std::uint32_t rows,
    std::vector<float> heights)
{
    return shark::terrain::HeightTile{
        .sample_columns = columns,
        .sample_rows = rows,
        .sample_spacing = 1.0F,
        .origin = {},
        .height_offsets = std::move(heights),
    };
}

[[nodiscard]] shark::terrain::HeightTileSurface make_flat_surface()
{
    auto result = shark::terrain::HeightTileSurface::create(
        make_tile(
            3U,
            3U,
            std::vector<float>(9U, 0.0F)));
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface make_ramp_surface()
{
    auto result = shark::terrain::HeightTileSurface::create(
        make_tile(
            3U,
            3U,
            {
                0.0F, 1.0F, 2.0F,
                0.0F, 1.0F, 2.0F,
                0.0F, 1.0F, 2.0F,
            }));
    REQUIRE(result);
    return std::move(result).value();
}

struct PartitionRun final {
    shark::physics::RigidBodyState moving_state;
    std::optional<shark::physics::BoxBoxContactManifold> final_contact;
    std::uint64_t step_count{};
    std::uint64_t contact_count{};
};

[[nodiscard]] PartitionRun run_partition_schedule(
    const std::uint32_t render_rate_hz,
    const std::uint32_t duration_seconds)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();

    const auto fixed_state = body_at({});
    const auto collider = box();
    physics::RigidBodyState moving_state{
        .position = {4.0F, 0.0F, 0.0F},
        .linear_velocity = {-1.0F, 0.0F, 0.0F},
    };
    std::optional<physics::BoxBoxContactManifold> final_contact;
    std::uint64_t contact_count = 0U;
    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;
    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result = clock.advance(
            timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;
        for (std::uint32_t step = 0U;
             step < frame_result.value().step_count;
             ++step) {
            REQUIRE(physics::advance_ballistic_body(
                moving_state,
                {},
                clock.fixed_delta_seconds()));
            const auto contact_result =
                physics::query_box_box_contact(
                    fixed_state,
                    collider,
                    moving_state,
                    collider);
            REQUIRE(contact_result);
            final_contact = contact_result.value();
            contact_count += final_contact.has_value() ? 1U : 0U;
        }
    }
    return PartitionRun{
        .moving_state = moving_state,
        .final_contact = final_contact,
        .step_count = clock.total_step_count(),
        .contact_count = contact_count,
    };
}

} // namespace

TEST_CASE(
    "box world geometry uses checked canonical axes and sign-bit vertices",
    "[physics][box][world-geometry]")
{
    using namespace shark;

    const auto state = body_at({10.0F, 20.0F, 30.0F});
    const auto collider = box({1.0F, 2.0F, 3.0F});
    const auto result = physics::make_box_world_geometry(
        state,
        collider);
    REQUIRE(result);
    const auto& geometry = result.value();
    require_float3(geometry.center, state.position);
    require_float3(geometry.axes[0], {1.0F, 0.0F, 0.0F});
    require_float3(geometry.axes[1], {0.0F, 1.0F, 0.0F});
    require_float3(geometry.axes[2], {0.0F, 0.0F, 1.0F});
    require_float3(geometry.half_extents, collider.local_half_extents);
    for (std::size_t vertex = 0U;
         vertex < geometry.vertices.size();
         ++vertex) {
        CAPTURE(vertex);
        require_float3(
            geometry.vertices[vertex],
            {
                10.0F + ((vertex & 1U) != 0U ? 1.0F : -1.0F),
                20.0F + ((vertex & 2U) != 0U ? 2.0F : -2.0F),
                30.0F + ((vertex & 4U) != 0U ? 3.0F : -3.0F),
            });
    }
    REQUIRE((geometry.bounds == terrain::Bounds3{
        .minimum = {9.0F, 18.0F, 27.0F},
        .maximum = {11.0F, 22.0F, 33.0F},
    }));

    const auto quarter_turn = axis_angle(
        {0.0F, 0.0F, 1.0F},
        math::half_pi);
    auto rotated_state = body_at({}, quarter_turn);
    auto equivalent_state = rotated_state;
    equivalent_state.orientation = {
        -quarter_turn.x,
        -quarter_turn.y,
        -quarter_turn.z,
        -quarter_turn.w,
    };
    const auto rotated = physics::make_box_world_geometry(
        rotated_state,
        collider);
    const auto equivalent = physics::make_box_world_geometry(
        equivalent_state,
        collider);
    REQUIRE(rotated);
    REQUIRE(equivalent);
    REQUIRE(rotated.value() == equivalent.value());
    require_float3(rotated.value().axes[0], {0.0F, 1.0F, 0.0F});
    require_float3(rotated.value().axes[1], {-1.0F, 0.0F, 0.0F});
    require_float3(rotated.value().axes[2], {0.0F, 0.0F, 1.0F});
}

TEST_CASE(
    "box world geometry rejects invalid and overflowing input transactionally",
    "[physics][box][world-geometry][validation][immutability]")
{
    using namespace shark;

    const auto valid_state = body_at({1.0F, 2.0F, 3.0F});
    const auto valid_box = box();
    REQUIRE(physics::is_valid(valid_box));

    SECTION("invalid rigid state")
    {
        auto state = valid_state;
        state.orientation.w = 2.0F;
        const auto before = state;
        const auto result = physics::make_box_world_geometry(
            state,
            valid_box);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == before);
    }

    SECTION("invalid extents")
    {
        for (const auto extent : {
                 0.0F,
                 -1.0F,
                 std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::infinity(),
             }) {
            CAPTURE(extent);
            auto collider = valid_box;
            collider.local_half_extents.y = extent;
            const auto before = collider;
            REQUIRE_FALSE(physics::is_valid(collider));
            const auto result = physics::make_box_world_geometry(
                valid_state,
                collider);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().code() ==
                core::ErrorCode::invalid_argument);
            if (std::isnan(extent)) {
                REQUIRE(std::isnan(
                    collider.local_half_extents.y));
            }
            else {
                REQUIRE(collider == before);
            }
        }
    }

    SECTION("finite corner overflow")
    {
        const auto maximum = std::numeric_limits<float>::max();
        const auto state = body_at({maximum, 0.0F, 0.0F});
        const auto collider = box({maximum, 1.0F, 1.0F});
        const auto state_before = state;
        const auto collider_before = collider;
        const auto result = physics::make_box_world_geometry(
            state,
            collider);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(state == state_before);
        REQUIRE(collider == collider_before);
    }
}

TEST_CASE(
    "axis-aligned box pairs distinguish separation tolerance and overlap",
    "[physics][box][pair][axis-aligned][tolerance]")
{
    using namespace shark;

    const auto first_state = body_at({});
    const auto collider = box();

    SECTION("separated")
    {
        const auto result = physics::query_box_box_contact(
            first_state,
            collider,
            body_at({2.01F, 0.0F, 0.0F}),
            collider);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }

    SECTION("outside tolerance")
    {
        const auto result = physics::query_box_box_contact(
            first_state,
            collider,
            body_at({2.00002F, 0.0F, 0.0F}),
            collider);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }

    SECTION("inside tolerance")
    {
        const auto result = physics::query_box_box_contact(
            first_state,
            collider,
            body_at({2.000005F, 0.0F, 0.0F}),
            collider);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation ==
            Catch::Approx(0.000005F).margin(0.000001F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.0F));
        require_box_manifold_consistent(manifold);
    }

    SECTION("penetrating")
    {
        const auto result = physics::query_box_box_contact(
            first_state,
            collider,
            body_at({1.5F, 0.0F, 0.0F}),
            collider);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxBoxSatFeature::first_face);
        REQUIRE(manifold.first_axis_index == 0U);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation == Catch::Approx(-0.5F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.5F));
        require_box_manifold_consistent(manifold);
    }
}

TEST_CASE(
    "touching box faces produce four stable witness points",
    "[physics][box][pair][face][manifold]")
{
    using namespace shark;

    const auto result = physics::query_box_box_contact(
        body_at({}),
        box(),
        body_at({2.0F, 0.0F, 0.0F}),
        box());
    REQUIRE(result);
    REQUIRE(result.value());
    const auto& manifold = *result.value();
    REQUIRE(manifold.feature ==
        physics::BoxBoxSatFeature::first_face);
    REQUIRE(manifold.first_axis_index == 0U);
    REQUIRE(manifold.point_count == 4U);
    require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
    REQUIRE(manifold.separation == Catch::Approx(0.0F));
    constexpr std::array expected_positions{
        shark::math::Float3{1.0F, -1.0F, -1.0F},
        shark::math::Float3{1.0F, 1.0F, -1.0F},
        shark::math::Float3{1.0F, 1.0F, 1.0F},
        shark::math::Float3{1.0F, -1.0F, 1.0F},
    };
    for (std::size_t index = 0U;
         index < manifold.point_count;
         ++index) {
        CAPTURE(index);
        require_float3(
            manifold.points[index].point_on_first,
            expected_positions[index]);
        require_float3(
            manifold.points[index].point_on_second,
            expected_positions[index]);
        require_float3(
            manifold.points[index].position,
            expected_positions[index]);
    }
    require_box_manifold_consistent(manifold);
}

TEST_CASE(
    "axis-aligned box edge and corner contacts reduce the manifold",
    "[physics][box][pair][edge][corner][manifold]")
{
    using namespace shark;

    SECTION("edge")
    {
        const auto result = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({2.0F, 2.0F, 0.0F}),
            box());
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.point_count == 2U);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        require_box_manifold_consistent(manifold);
    }

    SECTION("corner")
    {
        const auto result = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({2.0F, 2.0F, 2.0F}),
            box());
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.point_count == 1U);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        require_float3(
            manifold.points[0].position,
            {1.0F, 1.0F, 1.0F});
        require_box_manifold_consistent(manifold);
    }
}

TEST_CASE(
    "rotated boxes use full SAT including cross-axis separation",
    "[physics][box][pair][rotated][sat]")
{
    using namespace shark;

    SECTION("rotated face contact")
    {
        const auto rotation = axis_angle(
            {0.0F, 0.0F, 1.0F},
            math::pi * 0.25F);
        const auto result = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({2.3F, 0.0F, 0.0F}, rotation),
            box());
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxBoxSatFeature::first_face);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.penetration_depth ==
            Catch::Approx(0.1142135F).margin(0.00002F));
        require_box_manifold_consistent(manifold, 0.00002F);
    }

    SECTION("face axes overlap but a cross axis separates")
    {
        const math::Quaternion first_orientation{
            -0.11239134F,
            0.08672643F,
            0.20419474F,
            0.96858209F,
        };
        const math::Quaternion second_orientation{
            -0.40570295F,
            -0.20924843F,
            0.07734555F,
            0.88636214F,
        };
        const auto first_state = body_at({}, first_orientation);
        const auto second_state = body_at(
            {-1.91546869F, 0.78868359F, -1.97736263F},
            second_orientation);
        const auto first = box(
            {1.29502106F, 1.15167761F, 0.60226303F});
        const auto second = box(
            {0.79660088F, 0.64980286F, 1.23908591F});
        const auto first_geometry =
            physics::make_box_world_geometry(first_state, first);
        const auto second_geometry =
            physics::make_box_world_geometry(second_state, second);
        REQUIRE(first_geometry);
        REQUIRE(second_geometry);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            CAPTURE(axis);
            REQUIRE(projection_separation(
                first_geometry.value(),
                second_geometry.value(),
                first_geometry.value().axes[axis]) <= 0.0F);
            REQUIRE(projection_separation(
                first_geometry.value(),
                second_geometry.value(),
                second_geometry.value().axes[axis]) <= 0.0F);
        }
        const auto separating_cross = cross(
            first_geometry.value().axes[0],
            second_geometry.value().axes[2]);
        REQUIRE(projection_separation(
            first_geometry.value(),
            second_geometry.value(),
            separating_cross) > 0.5F);

        const auto result = physics::query_box_box_contact(
            first_state,
            first,
            second_state,
            second);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }
}

TEST_CASE(
    "box SAT exposes the deterministic winning edge pair",
    "[physics][box][pair][edge][sat]")
{
    using namespace shark;

    const auto first_state = body_at(
        {},
        {
            0.34277388F,
            0.31794646F,
            -0.23575117F,
            0.85196096F,
        });
    const auto second_state = body_at(
        {-0.36172134F, -0.40235648F, -1.89624846F},
        {
            -0.52715480F,
            -0.18759134F,
            -0.24796319F,
            0.79084229F,
        });
    const auto result = physics::query_box_box_contact(
        first_state,
        box({1.21469152F, 1.38316286F, 0.46268681F}),
        second_state,
        box({1.01541972F, 0.72709835F, 0.79276747F}));
    REQUIRE(result);
    REQUIRE(result.value());
    const auto& manifold = *result.value();
    REQUIRE(manifold.feature ==
        physics::BoxBoxSatFeature::edge_pair);
    REQUIRE(manifold.first_axis_index == 1U);
    REQUIRE(manifold.second_axis_index == 2U);
    REQUIRE(manifold.point_count == 1U);
    REQUIRE(manifold.penetration_depth ==
        Catch::Approx(0.7927567F).margin(0.00002F));
    require_box_manifold_consistent(manifold, 0.00002F);
}

TEST_CASE(
    "box SAT degeneracies and tied axes have stable fallbacks",
    "[physics][box][pair][coincident][near-parallel][tie]"
    "[determinism]")
{
    using namespace shark;

    SECTION("coincident")
    {
        const auto result = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({}),
            box());
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxBoxSatFeature::first_face);
        REQUIRE(manifold.first_axis_index == 0U);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation == Catch::Approx(-2.0F));
        require_box_manifold_consistent(manifold);
    }

    SECTION("near parallel")
    {
        const auto tiny_rotation = axis_angle(
            {0.0F, 0.0F, 1.0F},
            0.0000001F);
        const auto first = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({1.5F, 0.0F, 0.0F}, tiny_rotation),
            box());
        const auto repeated = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({1.5F, 0.0F, 0.0F}, tiny_rotation),
            box());
        REQUIRE(first);
        REQUIRE(repeated);
        REQUIRE(first.value());
        REQUIRE(first.value() == repeated.value());
        require_box_manifold_consistent(*first.value());
    }

    SECTION("equal face overlaps retain the first axis")
    {
        const auto result = physics::query_box_box_contact(
            body_at({}),
            box(),
            body_at({1.5F, 1.5F, 0.0F}),
            box());
        REQUIRE(result);
        REQUIRE(result.value());
        REQUIRE(result.value()->feature ==
            physics::BoxBoxSatFeature::first_face);
        REQUIRE(result.value()->first_axis_index == 0U);
        require_float3(
            result.value()->normal,
            {1.0F, 0.0F, 0.0F});
    }
}

TEST_CASE(
    "box pair swapping preserves witnesses and results repeat exactly",
    "[physics][box][pair][swap][determinism][immutability]")
{
    using namespace shark;

    const auto first_state = body_at({});
    const auto second_state = body_at({1.5F, 0.25F, -0.5F});
    const auto collider = box();
    const auto first_state_before = first_state;
    const auto second_state_before = second_state;
    const auto collider_before = collider;
    const auto forward = physics::query_box_box_contact(
        first_state,
        collider,
        second_state,
        collider);
    const auto reverse = physics::query_box_box_contact(
        second_state,
        collider,
        first_state,
        collider);
    REQUIRE(forward);
    REQUIRE(reverse);
    REQUIRE(forward.value());
    REQUIRE(reverse.value());
    require_float3(
        reverse.value()->normal,
        {
            -forward.value()->normal.x,
            -forward.value()->normal.y,
            -forward.value()->normal.z,
        });
    REQUIRE(reverse.value()->separation ==
        Catch::Approx(forward.value()->separation));
    REQUIRE(reverse.value()->penetration_depth ==
        Catch::Approx(forward.value()->penetration_depth));
    REQUIRE(reverse.value()->point_count ==
        forward.value()->point_count);
    std::array<bool, physics::box_contact_manifold_capacity> matched{};
    for (std::size_t forward_index = 0U;
         forward_index < forward.value()->point_count;
         ++forward_index) {
        CAPTURE(forward_index);
        std::optional<std::size_t> reverse_index;
        for (std::size_t candidate = 0U;
             candidate < reverse.value()->point_count;
             ++candidate) {
            if (!matched[candidate] && approximately_equal(
                    reverse.value()->points[candidate].position,
                    forward.value()->points[forward_index].position)) {
                reverse_index = candidate;
                break;
            }
        }
        REQUIRE(reverse_index);
        matched[*reverse_index] = true;
        require_float3(
            reverse.value()->points[*reverse_index].point_on_first,
            forward.value()->points[forward_index].point_on_second);
        require_float3(
            reverse.value()->points[*reverse_index].point_on_second,
            forward.value()->points[forward_index].point_on_first);
        require_float3(
            reverse.value()->points[*reverse_index].position,
            forward.value()->points[forward_index].position);
    }
    for (std::size_t repeat = 0U; repeat < 64U; ++repeat) {
        CAPTURE(repeat);
        const auto repeated = physics::query_box_box_contact(
            first_state,
            collider,
            second_state,
            collider);
        REQUIRE(repeated);
        REQUIRE(repeated.value() == forward.value());
    }
    REQUIRE(first_state == first_state_before);
    REQUIRE(second_state == second_state_before);
    REQUIRE(collider == collider_before);
}

TEST_CASE(
    "box terrain queries retain canonical flat and sloped witnesses",
    "[physics][box][terrain][flat][slope]")
{
    using namespace shark;

    SECTION("flat")
    {
        const auto surface = make_flat_surface();
        const auto result = physics::query_box_terrain_contact(
            body_at({0.5F, 1.0F, 0.5F}),
            box({0.25F, 1.0F, 0.25F}),
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxTerrainSatFeature::terrain_face);
        require_float3(manifold.normal, {0.0F, 1.0F, 0.0F});
        REQUIRE(manifold.separation == Catch::Approx(0.0F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.0F));
        for (std::size_t index = 0U;
             index < manifold.point_count;
             ++index) {
            REQUIRE(manifold.points[index].surface.cell_x == 0U);
            REQUIRE(manifold.points[index].surface.cell_z == 0U);
            REQUIRE(manifold.points[index].surface.triangle ==
                terrain::HeightTileTriangle::v00_v01_v11);
        }
        require_terrain_manifold_consistent(manifold);
    }

    SECTION("slope")
    {
        const auto surface = make_ramp_surface();
        constexpr auto inverse_sqrt_two = 0.7071067811865475F;
        const math::Float3 expected_normal{
            -inverse_sqrt_two,
            inverse_sqrt_two,
            0.0F,
        };
        constexpr float half_extent = 0.25F;
        const auto support_radius =
            half_extent * (std::abs(expected_normal.x) +
                           std::abs(expected_normal.y));
        const auto state = body_at({
            1.0F + expected_normal.x * support_radius,
            1.0F + expected_normal.y * support_radius,
            1.0F,
        });
        const auto result = physics::query_box_terrain_contact(
            state,
            box({half_extent, half_extent, half_extent}),
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        require_float3(manifold.normal, expected_normal, 0.00002F);
        REQUIRE(manifold.separation ==
            Catch::Approx(0.0F).margin(0.00002F));
        require_terrain_manifold_consistent(manifold, 0.00002F);
    }
}

TEST_CASE(
    "box terrain queries cover tilted bodies tile boundaries and misses",
    "[physics][box][terrain][tilted][boundary][outside]")
{
    using namespace shark;

    const auto surface = make_flat_surface();

    SECTION("tilted box")
    {
        const auto angle = math::pi / 6.0F;
        const auto rotation = axis_angle(
            {0.0F, 0.0F, 1.0F},
            angle);
        const auto vertical_radius =
            0.5F * (std::sin(angle) + std::cos(angle));
        const auto result = physics::query_box_terrain_contact(
            body_at({0.75F, vertical_radius, 0.75F}, rotation),
            box({0.5F, 0.5F, 0.5F}),
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        require_float3(
            result.value()->normal,
            {0.0F, 1.0F, 0.0F});
        REQUIRE(result.value()->separation ==
            Catch::Approx(0.0F).margin(0.00002F));
        require_terrain_manifold_consistent(
            *result.value(),
            0.00002F);
    }

    SECTION("maximum tile boundary")
    {
        const auto result = physics::query_box_terrain_contact(
            body_at({2.25F, 0.25F, 0.5F}),
            box({0.25F, 0.25F, 0.25F}),
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        for (std::size_t index = 0U;
             index < result.value()->point_count;
             ++index) {
            CAPTURE(index);
            REQUIRE(result.value()->points[index].surface.cell_x == 1U);
            REQUIRE(result.value()->points[index].surface.position.x ==
                Catch::Approx(2.0F));
        }
        require_terrain_manifold_consistent(*result.value());
    }

    SECTION("outside and vertically separated")
    {
        const auto outside = physics::query_box_terrain_contact(
            body_at({2.27F, 0.25F, 0.5F}),
            box({0.25F, 0.25F, 0.25F}),
            surface);
        const auto above = physics::query_box_terrain_contact(
            body_at({0.5F, 2.0F, 0.5F}),
            box({0.25F, 0.25F, 0.25F}),
            surface);
        REQUIRE(outside);
        REQUIRE(above);
        REQUIRE_FALSE(outside.value());
        REQUIRE_FALSE(above.value());
    }
}

TEST_CASE(
    "box pair queries reject invalid inputs without mutation",
    "[physics][box][pair][validation][immutability]")
{
    using namespace shark;

    auto first_state = body_at({});
    auto second_state = body_at({1.0F, 0.0F, 0.0F});
    auto first = box();
    auto second = box();
    const auto first_state_before = first_state;
    const auto second_state_before = second_state;
    const auto first_before = first;
    const auto second_before = second;
    const auto valid = physics::query_box_box_contact(
        first_state,
        first,
        second_state,
        second);
    REQUIRE(valid);
    REQUIRE(valid.value());
    REQUIRE(first_state == first_state_before);
    REQUIRE(second_state == second_state_before);
    REQUIRE(first == first_before);
    REQUIRE(second == second_before);

    second.local_half_extents.z = 0.0F;
    const auto invalid_second = second;
    const auto invalid_collider = physics::query_box_box_contact(
        first_state,
        first,
        second_state,
        second);
    REQUIRE_FALSE(invalid_collider);
    REQUIRE(invalid_collider.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(second == invalid_second);

    second = second_before;
    first_state.position.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_first_state = first_state;
    const auto invalid_state = physics::query_box_box_contact(
        first_state,
        first,
        second_state,
        second);
    REQUIRE_FALSE(invalid_state);
    REQUIRE(invalid_state.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(std::isnan(first_state.position.x));
    REQUIRE(first_state.position.y == invalid_first_state.position.y);
    REQUIRE(first_state.position.z == invalid_first_state.position.z);
    REQUIRE(first_state.orientation == invalid_first_state.orientation);
    REQUIRE(first == first_before);
    REQUIRE(second_state == second_state_before);
    REQUIRE(second == second_before);
}

TEST_CASE(
    "box terrain queries are transactional for valid and invalid input",
    "[physics][box][terrain][validation][immutability]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    auto state = body_at({0.5F, 0.5F, 0.5F});
    auto collider = box({0.5F, 0.5F, 0.5F});
    const auto tile_before = surface.tile();
    const auto bounds_before = surface.bounds();
    const auto state_before = state;
    const auto collider_before = collider;
    const auto valid = physics::query_box_terrain_contact(
        state,
        collider,
        surface);
    REQUIRE(valid);
    REQUIRE(valid.value());
    REQUIRE(state == state_before);
    REQUIRE(collider == collider_before);
    REQUIRE(surface.tile() == tile_before);
    REQUIRE(surface.bounds() == bounds_before);

    collider.local_half_extents.x = -1.0F;
    const auto invalid_collider = collider;
    const auto collider_result = physics::query_box_terrain_contact(
        state,
        collider,
        surface);
    REQUIRE_FALSE(collider_result);
    REQUIRE(collider_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(collider == invalid_collider);
    REQUIRE(surface.tile() == tile_before);

    collider = collider_before;
    state.orientation = {0.0F, 0.0F, 0.0F, 2.0F};
    const auto invalid_state = state;
    const auto state_result = physics::query_box_terrain_contact(
        state,
        collider,
        surface);
    REQUIRE_FALSE(state_result);
    REQUIRE(state_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(state == invalid_state);
    REQUIRE(collider == collider_before);
    REQUIRE(surface.tile() == tile_before);
    REQUIRE(surface.bounds() == bounds_before);
}

TEST_CASE(
    "box contact queries are invariant across render partitions",
    "[physics][box][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline = run_partition_schedule(render_rates[0], 3U);
    REQUIRE(baseline.step_count == 180U);
    REQUIRE(baseline.contact_count > 0U);
    REQUIRE(baseline.contact_count < baseline.step_count);
    REQUIRE(baseline.final_contact);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run = run_partition_schedule(render_rate, 3U);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.contact_count == baseline.contact_count);
        REQUIRE(run.moving_state == baseline.moving_state);
        REQUIRE(run.final_contact == baseline.final_contact);
    }
}

TEST_CASE(
    "box terrain tangent SAT features retain directional boundary witnesses",
    "[physics][box][terrain][box-face][edge-pair][boundary]"
    "[determinism][immutability]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    const auto tile_before = surface.tile();
    const auto bounds_before = surface.bounds();
    const auto collider = box({0.25F, 0.25F, 0.25F});
    const auto collider_before = collider;

    const auto require_finite_owned_point = [](
        const physics::BoxTerrainContactPoint& point,
        const std::uint32_t cell_x,
        const terrain::HeightTileTriangle triangle) {
        REQUIRE(math::is_finite(point.box_point));
        REQUIRE(math::is_finite(point.surface.position));
        REQUIRE(math::is_finite(point.surface.normal));
        REQUIRE(math::is_finite(point.surface.barycentrics));
        REQUIRE(math::is_finite(point.position));
        REQUIRE(std::isfinite(point.separation));
        REQUIRE(std::isfinite(point.penetration_depth));
        REQUIRE(point.surface.cell_x == cell_x);
        REQUIRE(point.surface.cell_z == 0U);
        REQUIRE(point.surface.triangle == triangle);
        require_float3(point.surface.normal, {0.0F, 1.0F, 0.0F});
        REQUIRE(point.surface.barycentrics.x >= -comparison_margin);
        REQUIRE(point.surface.barycentrics.y >= -comparison_margin);
        REQUIRE(point.surface.barycentrics.z >= -comparison_margin);
        REQUIRE(point.surface.barycentrics.x <= 1.0F + comparison_margin);
        REQUIRE(point.surface.barycentrics.y <= 1.0F + comparison_margin);
        REQUIRE(point.surface.barycentrics.z <= 1.0F + comparison_margin);
    };

    SECTION("penetrating box face points left at the minimum terrain boundary")
    {
        const auto state = body_at({-0.20F, 0.0F, 0.5F});
        const auto state_before = state;
        const auto result = physics::query_box_terrain_contact(
            state,
            collider,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxTerrainSatFeature::box_face);
        REQUIRE(manifold.box_axis_index == 0U);
        REQUIRE(manifold.point_count == 2U);
        require_float3(manifold.normal, {-1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation == Catch::Approx(-0.05F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.05F));
        require_terrain_manifold_consistent(manifold);
        for (std::size_t index = 0U; index < manifold.point_count; ++index) {
            CAPTURE(index);
            const auto& point = manifold.points[index];
            require_finite_owned_point(
                point,
                0U,
                terrain::HeightTileTriangle::v00_v01_v11);
            REQUIRE(point.box_point.x == Catch::Approx(0.05F));
            REQUIRE(point.box_point.y == Catch::Approx(0.0F));
            REQUIRE(point.box_point.z ==
                Catch::Approx(point.surface.position.z));
            REQUIRE(point.surface.position.x == Catch::Approx(0.0F));
            REQUIRE(point.surface.position.y == Catch::Approx(0.0F));
        }
        REQUIRE(manifold.points[0].surface.position.z ==
            Catch::Approx(0.25F));
        REQUIRE(manifold.points[1].surface.position.z ==
            Catch::Approx(0.75F));
        for (std::size_t repeat = 0U; repeat < 32U; ++repeat) {
            CAPTURE(repeat);
            const auto repeated = physics::query_box_terrain_contact(
                state,
                collider,
                surface);
            REQUIRE(repeated);
            REQUIRE(repeated.value() == result.value());
        }
        REQUIRE(state == state_before);
    }

    SECTION("box face points right at the maximum terrain boundary")
    {
        const auto state = body_at({2.25F, 0.0F, 0.5F});
        const auto state_before = state;
        const auto result = physics::query_box_terrain_contact(
            state,
            collider,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxTerrainSatFeature::box_face);
        REQUIRE(manifold.box_axis_index == 0U);
        REQUIRE(manifold.point_count == 2U);
        require_float3(manifold.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation == Catch::Approx(0.0F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.0F));
        require_terrain_manifold_consistent(manifold);
        for (std::size_t index = 0U; index < manifold.point_count; ++index) {
            CAPTURE(index);
            const auto& point = manifold.points[index];
            require_finite_owned_point(
                point,
                1U,
                terrain::HeightTileTriangle::v00_v11_v10);
            require_float3(point.box_point, point.surface.position);
            REQUIRE(point.surface.position.x == Catch::Approx(2.0F));
            REQUIRE(point.surface.position.y == Catch::Approx(0.0F));
        }
        REQUIRE(manifold.points[0].surface.position.z ==
            Catch::Approx(0.75F));
        REQUIRE(manifold.points[1].surface.position.z ==
            Catch::Approx(0.25F));
        for (std::size_t repeat = 0U; repeat < 32U; ++repeat) {
            CAPTURE(repeat);
            const auto repeated = physics::query_box_terrain_contact(
                state,
                collider,
                surface);
            REQUIRE(repeated);
            REQUIRE(repeated.value() == result.value());
        }
        REQUIRE(state == state_before);
    }

    SECTION("edge pair points left from the minimum terrain edge")
    {
        const auto angle = math::pi / 6.0F;
        const auto projected_radius =
            0.25F * (std::cos(angle) + std::sin(angle));
        const auto expected_z =
            0.5F + 0.25F * (std::cos(angle) - std::sin(angle));
        const auto state = body_at(
            {-projected_radius, 0.0F, 0.5F},
            axis_angle({0.0F, 1.0F, 0.0F}, angle));
        const auto state_before = state;
        const auto result = physics::query_box_terrain_contact(
            state,
            collider,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxTerrainSatFeature::edge_pair);
        REQUIRE(manifold.box_axis_index == 1U);
        REQUIRE(manifold.triangle_edge_index == 0U);
        REQUIRE(manifold.point_count == 1U);
        require_float3(manifold.normal, {-1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.separation ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.0F));
        require_terrain_manifold_consistent(manifold);
        const auto& point = manifold.points[0];
        require_finite_owned_point(
            point,
            0U,
            terrain::HeightTileTriangle::v00_v01_v11);
        REQUIRE(point.box_point.x ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(point.box_point.y == Catch::Approx(0.0F));
        REQUIRE(point.box_point.z ==
            Catch::Approx(expected_z).margin(0.000001F));
        require_float3(
            point.surface.position,
            {0.0F, 0.0F, expected_z},
            0.000001F);
        REQUIRE(point.surface.barycentrics.x ==
            Catch::Approx(1.0F - expected_z).margin(0.000001F));
        REQUIRE(point.surface.barycentrics.y ==
            Catch::Approx(expected_z).margin(0.000001F));
        REQUIRE(point.surface.barycentrics.z ==
            Catch::Approx(0.0F).margin(0.000001F));
        for (std::size_t repeat = 0U; repeat < 32U; ++repeat) {
            CAPTURE(repeat);
            const auto repeated = physics::query_box_terrain_contact(
                state,
                collider,
                surface);
            REQUIRE(repeated);
            REQUIRE(repeated.value() == result.value());
        }
        REQUIRE(state == state_before);
    }

    SECTION("near-tangent box face remains in the terrain hemisphere")
    {
        constexpr auto near_tangent_angle = 5.0e-13F;
        const auto projected_radius = 0.25F *
            (std::cos(near_tangent_angle) +
             std::sin(near_tangent_angle));
        const auto state = body_at(
            {-projected_radius, 0.0F, 0.5F},
            axis_angle(
                {0.0F, 0.0F, 1.0F},
                near_tangent_angle));
        const auto state_before = state;
        const auto result = physics::query_box_terrain_contact(
            state,
            collider,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& manifold = *result.value();
        REQUIRE(manifold.feature ==
            physics::BoxTerrainSatFeature::box_face);
        REQUIRE(manifold.box_axis_index == 0U);
        REQUIRE(manifold.point_count == 2U);
        REQUIRE(math::is_finite(manifold.normal));
        require_unit_vector(manifold.normal);
        require_float3(manifold.normal, {-1.0F, 0.0F, 0.0F});
        REQUIRE(manifold.normal.y >= 0.0F);
        REQUIRE(manifold.separation ==
            Catch::Approx(0.0F).margin(0.000001F));
        REQUIRE(manifold.penetration_depth == Catch::Approx(0.0F));
        require_terrain_manifold_consistent(manifold);
        for (std::size_t index = 0U; index < manifold.point_count; ++index) {
            CAPTURE(index);
            const auto& point = manifold.points[index];
            require_finite_owned_point(
                point,
                0U,
                terrain::HeightTileTriangle::v00_v01_v11);
            REQUIRE(dot(manifold.normal, point.surface.normal) >= 0.0F);
            REQUIRE(point.separation ==
                Catch::Approx(dot(
                    subtract(point.box_point, point.surface.position),
                    manifold.normal)).margin(0.000001F));
            REQUIRE(point.penetration_depth ==
                Catch::Approx(std::max(0.0F, -point.separation))
                    .margin(0.000001F));
        }
        for (std::size_t repeat = 0U; repeat < 32U; ++repeat) {
            CAPTURE(repeat);
            const auto repeated = physics::query_box_terrain_contact(
                state,
                collider,
                surface);
            REQUIRE(repeated);
            REQUIRE(repeated.value() == result.value());
        }
        REQUIRE(state == state_before);
    }

    REQUIRE(collider == collider_before);
    REQUIRE(surface.tile() == tile_before);
    REQUIRE(surface.bounds() == bounds_before);
}
