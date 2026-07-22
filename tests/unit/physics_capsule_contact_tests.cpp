#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/capsule_contact.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
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

void require_unit_vector(const shark::math::Float3 value)
{
    const auto length_squared =
        value.x * value.x +
        value.y * value.y +
        value.z * value.z;
    REQUIRE(length_squared == Catch::Approx(1.0F).margin(
        comparison_margin));
}

[[nodiscard]] shark::physics::RigidBodyState body_at(
    const shark::math::Float3 position)
{
    return shark::physics::RigidBodyState{
        .position = position,
    };
}

[[nodiscard]] shark::physics::CapsuleCollider vertical_capsule(
    const float radius = 1.0F,
    const float half_length = 1.0F)
{
    return shark::physics::CapsuleCollider{
        .local_half_segment = {0.0F, half_length, 0.0F},
        .radius = radius,
    };
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
            3,
            3,
            {
                0.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 0.0F,
            }));
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface make_ramp_surface()
{
    // height = x, so every canonical triangle has normal (-1, 1, 0).
    auto result = shark::terrain::HeightTileSurface::create(
        make_tile(
            3,
            3,
            {
                0.0F, 1.0F, 2.0F,
                0.0F, 1.0F, 2.0F,
                0.0F, 1.0F, 2.0F,
            }));
    REQUIRE(result);
    return std::move(result).value();
}

struct PartitionRun final {
    shark::physics::RigidBodyState sphere_state;
    std::optional<shark::physics::CapsuleSphereContact> final_contact;
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

    const auto capsule_state = body_at({});
    const auto capsule = vertical_capsule();
    physics::RigidBodyState sphere_state{
        .position = {4.0F, 0.0F, 0.0F},
        .linear_velocity = {-1.0F, 0.0F, 0.0F},
    };
    constexpr physics::SphereCollider sphere{1.0F};
    std::optional<physics::CapsuleSphereContact> final_contact;
    std::uint64_t contact_count = 0;
    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;

    for (std::uint64_t frame = 1;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result = clock.advance(
            timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;
        for (std::uint32_t step = 0;
             step < frame_result.value().step_count;
             ++step) {
            REQUIRE(physics::advance_ballistic_body(
                sphere_state,
                {},
                clock.fixed_delta_seconds()));
            const auto contact_result =
                physics::query_capsule_sphere_contact(
                    capsule_state,
                    capsule,
                    sphere_state,
                    sphere);
            REQUIRE(contact_result);
            final_contact = contact_result.value();
            contact_count += final_contact.has_value() ? 1U : 0U;
        }
    }

    return PartitionRun{
        .sphere_state = sphere_state,
        .final_contact = final_contact,
        .step_count = clock.total_step_count(),
        .contact_count = contact_count,
    };
}

} // namespace

TEST_CASE(
    "capsule world endpoints apply normalized body orientation",
    "[physics][capsule][world-segment]")
{
    using namespace shark;

    const auto collider = vertical_capsule(0.5F, 2.0F);

    SECTION("identity")
    {
        const auto state = body_at({10.0F, 20.0F, 30.0F});
        const auto result = physics::make_capsule_world_segment(
            state,
            collider);
        REQUIRE(result);
        require_float3(
            result.value().first_endpoint,
            {10.0F, 18.0F, 30.0F});
        require_float3(
            result.value().second_endpoint,
            {10.0F, 22.0F, 30.0F});
    }

    SECTION("quarter turn and quaternion sign equivalence")
    {
        const auto sine = std::sin(math::pi * 0.25F);
        const auto cosine = std::cos(math::pi * 0.25F);
        auto state = body_at({10.0F, 20.0F, 30.0F});
        state.orientation = {0.0F, 0.0F, sine, cosine};
        auto equivalent_state = state;
        equivalent_state.orientation = {
            -state.orientation.x,
            -state.orientation.y,
            -state.orientation.z,
            -state.orientation.w,
        };

        const auto result = physics::make_capsule_world_segment(
            state,
            collider);
        const auto equivalent_result =
            physics::make_capsule_world_segment(
                equivalent_state,
                collider);
        REQUIRE(result);
        REQUIRE(equivalent_result);
        require_float3(
            result.value().first_endpoint,
            {12.0F, 20.0F, 30.0F});
        require_float3(
            result.value().second_endpoint,
            {8.0F, 20.0F, 30.0F});
        require_float3(
            equivalent_result.value().first_endpoint,
            result.value().first_endpoint);
        require_float3(
            equivalent_result.value().second_endpoint,
            result.value().second_endpoint);
    }

    SECTION("zero half segment")
    {
        const auto state = body_at({-3.0F, 4.0F, 5.0F});
        constexpr physics::CapsuleCollider degenerate{
            .local_half_segment = {},
            .radius = 1.0F,
        };
        const auto result = physics::make_capsule_world_segment(
            state,
            degenerate);
        REQUIRE(result);
        REQUIRE(result.value().first_endpoint == state.position);
        REQUIRE(result.value().second_endpoint == state.position);
    }
}

TEST_CASE(
    "capsule world endpoint validation is transactional",
    "[physics][capsule][world-segment][validation]")
{
    using namespace shark;

    const auto valid_state = body_at({1.0F, 2.0F, 3.0F});
    const auto valid_collider = vertical_capsule();
    REQUIRE(physics::is_valid(valid_collider));

    SECTION("invalid rigid state")
    {
        auto state = valid_state;
        state.orientation.w = 2.0F;
        const auto before = state;
        const auto result = physics::make_capsule_world_segment(
            state,
            valid_collider);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == before);
    }

    SECTION("nonfinite local segment")
    {
        auto collider = valid_collider;
        collider.local_half_segment.x =
            std::numeric_limits<float>::infinity();
        const auto before = collider;
        REQUIRE_FALSE(physics::is_valid(collider));
        const auto result = physics::make_capsule_world_segment(
            valid_state,
            collider);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(collider == before);
    }

    SECTION("nonpositive or nonfinite radius")
    {
        for (const auto radius : {
                 0.0F,
                 -1.0F,
                 std::numeric_limits<float>::quiet_NaN(),
             }) {
            CAPTURE(radius);
            auto collider = valid_collider;
            collider.radius = radius;
            const auto result = physics::make_capsule_world_segment(
                valid_state,
                collider);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().code() ==
                core::ErrorCode::invalid_argument);
        }
    }

    SECTION("finite inputs overflow a world endpoint")
    {
        const auto maximum = std::numeric_limits<float>::max();
        const auto state = body_at({maximum, 0.0F, 0.0F});
        const physics::CapsuleCollider collider{
            .local_half_segment = {maximum, 0.0F, 0.0F},
            .radius = 1.0F,
        };
        const auto result = physics::make_capsule_world_segment(
            state,
            collider);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
    }

    SECTION("finite rotation exceeds float range")
    {
        const auto maximum = std::numeric_limits<float>::max();
        auto state = body_at({});
        state.orientation = {
            0.0F,
            0.0F,
            std::sin(math::pi * 0.125F),
            std::cos(math::pi * 0.125F),
        };
        const physics::CapsuleCollider collider{
            .local_half_segment = {maximum, maximum, 0.0F},
            .radius = 1.0F,
        };
        const auto result = physics::make_capsule_world_segment(
            state,
            collider);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
    }
}

TEST_CASE(
    "capsule sphere queries cover body and endpoint features",
    "[physics][capsule][sphere][closest-feature]")
{
    using namespace shark;

    const auto capsule_state = body_at({});
    const auto capsule = vertical_capsule();
    constexpr physics::SphereCollider sphere{1.0F};

    SECTION("separated")
    {
        const auto sphere_state = body_at({3.0F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_sphere_contact(
            capsule_state,
            capsule,
            sphere_state,
            sphere);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }

    SECTION("touching capsule body")
    {
        const auto sphere_state = body_at({2.0F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_sphere_contact(
            capsule_state,
            capsule,
            sphere_state,
            sphere);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.capsule_axis_point, {});
        require_float3(contact.sphere_center, sphere_state.position);
        require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
        require_unit_vector(contact.normal);
        REQUIRE(contact.separation == Catch::Approx(0.0F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.0F));
    }

    SECTION("penetrating capsule body")
    {
        const auto sphere_state = body_at({1.25F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_sphere_contact(
            capsule_state,
            capsule,
            sphere_state,
            sphere);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.capsule_axis_point, {});
        require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(-0.75F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.75F));
    }

    SECTION("touching endpoint")
    {
        const auto sphere_state = body_at({0.0F, 3.0F, 0.0F});
        const auto result = physics::query_capsule_sphere_contact(
            capsule_state,
            capsule,
            sphere_state,
            sphere);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.capsule_axis_point,
            {0.0F, 1.0F, 0.0F});
        require_float3(contact.normal, {0.0F, 1.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.0F));
    }
}

TEST_CASE(
    "capsule sphere coincident fallbacks are deterministic",
    "[physics][capsule][sphere][coincident][degenerate][determinism]")
{
    using namespace shark;

    const auto capsule_state = body_at({});
    const auto sphere_state = body_at({});
    constexpr physics::SphereCollider sphere{1.0F};

    SECTION("nondegenerate capsule uses an axis perpendicular")
    {
        const auto result = physics::query_capsule_sphere_contact(
            capsule_state,
            vertical_capsule(),
            sphere_state,
            sphere);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.capsule_axis_point, {});
        require_float3(contact.normal, {0.0F, 0.0F, -1.0F});
        require_unit_vector(contact.normal);
        REQUIRE(contact.separation == Catch::Approx(-2.0F));
        REQUIRE(contact.penetration_depth == Catch::Approx(2.0F));
    }

    SECTION("zero-length capsule uses positive X")
    {
        constexpr physics::CapsuleCollider degenerate{
            .local_half_segment = {},
            .radius = 1.0F,
        };
        const auto first = physics::query_capsule_sphere_contact(
            capsule_state,
            degenerate,
            sphere_state,
            sphere);
        const auto second = physics::query_capsule_sphere_contact(
            capsule_state,
            degenerate,
            sphere_state,
            sphere);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first.value());
        REQUIRE(second.value());
        REQUIRE(first.value() == second.value());
        require_float3(
            first.value()->normal,
            {1.0F, 0.0F, 0.0F});
    }
}

TEST_CASE(
    "capsule sphere queries reject invalid input without mutation",
    "[physics][capsule][sphere][validation][immutability]")
{
    using namespace shark;

    auto capsule_state = body_at({});
    auto capsule = vertical_capsule();
    auto sphere_state = body_at({1.0F, 0.0F, 0.0F});
    physics::SphereCollider sphere{1.0F};
    const auto capsule_state_before = capsule_state;
    const auto capsule_before = capsule;
    const auto sphere_state_before = sphere_state;
    const auto sphere_before = sphere;

    const auto valid_result = physics::query_capsule_sphere_contact(
        capsule_state,
        capsule,
        sphere_state,
        sphere);
    REQUIRE(valid_result);
    REQUIRE(valid_result.value());
    REQUIRE(capsule_state == capsule_state_before);
    REQUIRE(capsule == capsule_before);
    REQUIRE(sphere_state == sphere_state_before);
    REQUIRE(sphere == sphere_before);

    sphere.radius = 0.0F;
    const auto invalid_sphere = sphere;
    const auto radius_result = physics::query_capsule_sphere_contact(
        capsule_state,
        capsule,
        sphere_state,
        sphere);
    REQUIRE_FALSE(radius_result);
    REQUIRE(radius_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(capsule_state == capsule_state_before);
    REQUIRE(capsule == capsule_before);
    REQUIRE(sphere_state == sphere_state_before);
    REQUIRE(sphere == invalid_sphere);

    sphere = sphere_before;
    sphere_state.position.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_state = sphere_state;
    const auto state_result = physics::query_capsule_sphere_contact(
        capsule_state,
        capsule,
        sphere_state,
        sphere);
    REQUIRE_FALSE(state_result);
    REQUIRE(state_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(std::isnan(sphere_state.position.x));
    REQUIRE(sphere_state.position.y == invalid_state.position.y);
    REQUIRE(sphere_state.position.z == invalid_state.position.z);
    REQUIRE(sphere_state.orientation == invalid_state.orientation);
    REQUIRE(
        sphere_state.linear_velocity ==
        invalid_state.linear_velocity);
    REQUIRE(
        sphere_state.angular_velocity ==
        invalid_state.angular_velocity);
}

TEST_CASE(
    "capsule pair queries cover parallel crossed and endpoint features",
    "[physics][capsule][capsule][closest-feature]")
{
    using namespace shark;

    const auto first_state = body_at({});
    const auto vertical = vertical_capsule();

    SECTION("separated parallel capsules")
    {
        const auto second_state = body_at({3.0F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            vertical,
            second_state,
            vertical);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }

    SECTION("touching parallel capsules")
    {
        const auto second_state = body_at({2.0F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            vertical,
            second_state,
            vertical);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.first_axis_point,
            {0.0F, -1.0F, 0.0F});
        require_float3(
            contact.second_axis_point,
            {2.0F, -1.0F, 0.0F});
        require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.0F));
    }

    SECTION("penetrating parallel capsules")
    {
        const auto second_state = body_at({1.5F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            vertical,
            second_state,
            vertical);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(-0.5F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.5F));
    }

    SECTION("touching crossed capsules")
    {
        const auto second_state = body_at({0.0F, 0.0F, 2.0F});
        constexpr physics::CapsuleCollider horizontal{
            .local_half_segment = {1.0F, 0.0F, 0.0F},
            .radius = 1.0F,
        };
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            vertical,
            second_state,
            horizontal);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.first_axis_point, {});
        require_float3(
            contact.second_axis_point,
            {0.0F, 0.0F, 2.0F});
        require_float3(contact.normal, {0.0F, 0.0F, 1.0F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
    }

    SECTION("touching endpoints")
    {
        const auto second_state = body_at({0.0F, 4.0F, 0.0F});
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            vertical,
            second_state,
            vertical);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.first_axis_point,
            {0.0F, 1.0F, 0.0F});
        require_float3(
            contact.second_axis_point,
            {0.0F, 3.0F, 0.0F});
        require_float3(contact.normal, {0.0F, 1.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
    }
}

TEST_CASE(
    "capsule pair degenerate and coincident fallbacks are deterministic",
    "[physics][capsule][capsule][degenerate][coincident][determinism]")
{
    using namespace shark;

    SECTION("degenerate capsules reduce to sphere centers")
    {
        constexpr physics::CapsuleCollider degenerate{
            .local_half_segment = {},
            .radius = 1.0F,
        };
        const auto first_state = body_at({});
        const auto second_state = body_at({2.0F, 0.0F, 0.0F});
        const auto result = physics::query_capsule_capsule_contact(
            first_state,
            degenerate,
            second_state,
            degenerate);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(contact.first_axis_point, {});
        require_float3(
            contact.second_axis_point,
            {2.0F, 0.0F, 0.0F});
        require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
    }

    SECTION("coincident parallel axes use a fixed perpendicular")
    {
        const auto state = body_at({});
        const auto collider = vertical_capsule();
        const auto first = physics::query_capsule_capsule_contact(
            state,
            collider,
            state,
            collider);
        const auto second = physics::query_capsule_capsule_contact(
            state,
            collider,
            state,
            collider);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first.value());
        REQUIRE(second.value());
        REQUIRE(first.value() == second.value());
        require_float3(
            first.value()->normal,
            {0.0F, 0.0F, -1.0F});
        require_unit_vector(first.value()->normal);
        REQUIRE(first.value()->separation == Catch::Approx(-2.0F));
        REQUIRE(first.value()->penetration_depth ==
            Catch::Approx(2.0F));
    }
}

TEST_CASE(
    "capsule pair queries reject invalid input without mutation",
    "[physics][capsule][capsule][validation][immutability]")
{
    using namespace shark;

    auto first_state = body_at({});
    auto first = vertical_capsule();
    auto second_state = body_at({1.0F, 0.0F, 0.0F});
    auto second = vertical_capsule();
    const auto first_state_before = first_state;
    const auto first_before = first;
    const auto second_state_before = second_state;
    const auto second_before = second;

    const auto valid_result = physics::query_capsule_capsule_contact(
        first_state,
        first,
        second_state,
        second);
    REQUIRE(valid_result);
    REQUIRE(valid_result.value());
    REQUIRE(first_state == first_state_before);
    REQUIRE(first == first_before);
    REQUIRE(second_state == second_state_before);
    REQUIRE(second == second_before);

    second.radius = 0.0F;
    const auto invalid_second = second;
    const auto collider_result =
        physics::query_capsule_capsule_contact(
            first_state,
            first,
            second_state,
            second);
    REQUIRE_FALSE(collider_result);
    REQUIRE(collider_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(first_state == first_state_before);
    REQUIRE(first == first_before);
    REQUIRE(second_state == second_state_before);
    REQUIRE(second == invalid_second);

    second = second_before;
    first_state.orientation = {};
    first_state.orientation.w = 2.0F;
    const auto invalid_first_state = first_state;
    const auto state_result = physics::query_capsule_capsule_contact(
        first_state,
        first,
        second_state,
        second);
    REQUIRE_FALSE(state_result);
    REQUIRE(state_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(first_state == invalid_first_state);
}

TEST_CASE(
    "capsule terrain queries retain canonical face and slope witnesses",
    "[physics][capsule][terrain][face][slope]")
{
    using namespace shark;

    SECTION("flat face")
    {
        const auto surface = make_flat_surface();
        const auto state = body_at({1.0F, 1.0F, 1.0F});
        constexpr physics::CapsuleCollider capsule{
            .local_half_segment = {0.5F, 0.0F, 0.0F},
            .radius = 1.0F,
        };
        const auto result = physics::query_capsule_terrain_contact(
            state,
            capsule,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.capsule_axis_point,
            {0.5F, 1.0F, 1.0F});
        require_float3(
            contact.surface.position,
            {0.5F, 0.0F, 1.0F});
        require_float3(
            contact.surface.normal,
            {0.0F, 1.0F, 0.0F});
        require_float3(contact.normal, contact.surface.normal);
        REQUIRE(contact.surface.cell_x == 0);
        REQUIRE(contact.surface.cell_z == 0);
        REQUIRE(contact.surface.triangle ==
            terrain::HeightTileTriangle::v00_v01_v11);
        require_float3(
            contact.surface.barycentrics,
            {0.0F, 0.5F, 0.5F});
        REQUIRE(contact.separation == Catch::Approx(0.0F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.0F));
    }

    SECTION("sloped face")
    {
        const auto surface = make_ramp_surface();
        constexpr auto inverse_square_root_two = 0.7071067811865475F;
        const math::Float3 expected_normal{
            -inverse_square_root_two,
            inverse_square_root_two,
            0.0F,
        };
        const auto state = body_at({
            1.0F + expected_normal.x,
            1.0F + expected_normal.y,
            1.0F,
        });
        constexpr physics::CapsuleCollider capsule{
            .local_half_segment = {},
            .radius = 1.0F,
        };
        const auto result = physics::query_capsule_terrain_contact(
            state,
            capsule,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.surface.position,
            {1.0F, 1.0F, 1.0F},
            0.00002F);
        require_float3(contact.surface.normal, expected_normal);
        require_float3(contact.normal, expected_normal);
        require_unit_vector(contact.normal);
        REQUIRE(contact.separation ==
            Catch::Approx(0.0F).margin(0.00001F));
        REQUIRE(contact.penetration_depth ==
            Catch::Approx(0.0F).margin(0.00001F));
    }
}

TEST_CASE(
    "capsule terrain zero-distance and invalid cases are explicit",
    "[physics][capsule][terrain][zero-distance][validation]"
    "[immutability]")
{
    using namespace shark;

    const auto surface = make_flat_surface();

    SECTION("axis intersects canonical face")
    {
        const auto state = body_at({0.5F, 0.5F, 0.75F});
        constexpr physics::CapsuleCollider capsule{
            .local_half_segment = {0.0F, 1.0F, 0.0F},
            .radius = 0.25F,
        };
        const auto result = physics::query_capsule_terrain_contact(
            state,
            capsule,
            surface);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& contact = *result.value();
        require_float3(
            contact.capsule_axis_point,
            {0.5F, 0.0F, 0.75F});
        require_float3(
            contact.surface.position,
            contact.capsule_axis_point);
        require_float3(contact.normal, {0.0F, 1.0F, 0.0F});
        REQUIRE(contact.separation == Catch::Approx(-0.25F));
        REQUIRE(contact.penetration_depth == Catch::Approx(0.25F));
    }

    SECTION("invalid state")
    {
        auto state = body_at({0.5F, 1.0F, 0.75F});
        state.position.y =
            std::numeric_limits<float>::infinity();
        const auto before = state;
        const auto capsule = vertical_capsule();
        const auto result = physics::query_capsule_terrain_contact(
            state,
            capsule,
            surface);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == before);
    }

    SECTION("invalid collider")
    {
        const auto state = body_at({0.5F, 1.0F, 0.75F});
        auto capsule = vertical_capsule();
        capsule.radius = 0.0F;
        const auto before = capsule;
        const auto result = physics::query_capsule_terrain_contact(
            state,
            capsule,
            surface);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(capsule == before);
    }
}

TEST_CASE(
    "capsule contact queries are invariant across render partitions",
    "[physics][capsule][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };
    const auto baseline = run_partition_schedule(render_rates[0], 3);
    REQUIRE(baseline.step_count == 180);
    REQUIRE(baseline.contact_count > 0);
    REQUIRE(baseline.contact_count < baseline.step_count);
    REQUIRE(baseline.final_contact);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run = run_partition_schedule(render_rate, 3);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.contact_count == baseline.contact_count);
        REQUIRE(run.sphere_state == baseline.sphere_state);
        REQUIRE(run.final_contact == baseline.final_contact);
    }
}
