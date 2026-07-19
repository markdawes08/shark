#include <shark/physics/sphere_terrain_contact.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/world/environment_lab_scenario.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    // height = x, so both triangles have the exact normal
    // normalize((-1, 1, 0)).
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

[[nodiscard]] shark::terrain::HeightTileSurface make_twisted_surface()
{
    auto result = shark::terrain::HeightTileSurface::create(
        make_tile(
            2,
            2,
            {
                0.0F, 0.0F,
                0.0F, 1.0F,
            }));
    REQUIRE(result);
    return std::move(result).value();
}

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

[[nodiscard]] double center_plane_distance(
    const shark::physics::BallisticBodyState& state,
    const shark::terrain::HeightTileSurfaceSample& surface)
{
    return
        (static_cast<double>(state.position.x) -
         static_cast<double>(surface.position.x)) *
            static_cast<double>(surface.normal.x) +
        (static_cast<double>(state.position.y) -
         static_cast<double>(surface.position.y)) *
            static_cast<double>(surface.normal.y) +
        (static_cast<double>(state.position.z) -
         static_cast<double>(surface.position.z)) *
            static_cast<double>(surface.normal.z);
}

struct ContactRun final {
    shark::physics::BallisticBodyState state;
    std::uint64_t step_count{};
    std::uint64_t contact_count{};
};

[[nodiscard]] ContactRun run_contact_schedule(
    const shark::terrain::HeightTileSurface& surface,
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
    physics::BallisticBodyState state{
        .position = {1.0F, 3.0F, 1.0F},
    };
    constexpr physics::SphereCollider collider{1.0F};
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
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;
        for (std::uint32_t step = 0;
             step < frame_result.value().step_count;
             ++step) {
            const auto result =
                physics::advance_sphere_against_terrain(
                    state,
                    collider,
                    surface,
                    physics::standard_gravity,
                    clock.fixed_delta_seconds());
            REQUIRE(result);
            contact_count +=
                result.value().contact.has_value() ? 1U : 0U;
        }
    }

    return ContactRun{
        .state = state,
        .step_count = clock.total_step_count(),
        .contact_count = contact_count,
    };
}

} // namespace

TEST_CASE(
    "sphere falls onto flat canonical terrain and remains supported",
    "[physics][sphere][terrain][rest]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    constexpr physics::SphereCollider collider{1.0F};
    physics::BallisticBodyState state{
        .position = {1.0F, 3.0F, 1.0F},
    };
    std::uint32_t first_contact_tick = 0;
    constexpr std::uint32_t total_ticks = 600;

    for (std::uint32_t tick = 1;
         tick <= total_ticks;
         ++tick) {
        const auto result =
            physics::advance_sphere_against_terrain(
                state,
                collider,
                surface,
                physics::standard_gravity,
                1.0F / 60.0F);
        REQUIRE(result);
        if (result.value().contact.has_value()) {
            if (first_contact_tick == 0U) {
                first_contact_tick = tick;
            }
            const auto& contact = *result.value().contact;
            REQUIRE(
                center_plane_distance(state, contact.surface) ==
                Catch::Approx(collider.radius).margin(0.000001));
            REQUIRE(
                contact.plane_separation_before_resolution <=
                comparison_margin);
            REQUIRE(contact.penetration_depth >= 0.0F);
            const auto expected_penetration = std::max(
                0.0F,
                -contact.plane_separation_before_resolution);
            REQUIRE(
                contact.penetration_depth ==
                Catch::Approx(expected_penetration)
                    .margin(0.000001));
            REQUIRE(state.linear_velocity == math::Float3{});
        }
        else {
            REQUIRE(first_contact_tick == 0U);
        }
    }

    REQUIRE(first_contact_tick == 38);
    require_float3(state.position, {1.0F, 1.0F, 1.0F});
    REQUIRE(state.linear_velocity == math::Float3{});
}

TEST_CASE(
    "terrain support is invariant across render-rate partitions",
    "[physics][sphere][terrain][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };
    const auto surface = make_flat_surface();
    const auto baseline =
        run_contact_schedule(surface, render_rates[0], 4);
    REQUIRE(baseline.step_count == 240);
    REQUIRE(baseline.contact_count > 0);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run =
            run_contact_schedule(surface, render_rate, 4);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.contact_count == baseline.contact_count);
        REQUIRE(run.state == baseline.state);
    }
    require_float3(
        baseline.state.position,
        {1.0F, 1.0F, 1.0F});
    REQUIRE(
        baseline.state.linear_velocity ==
        shark::math::Float3{});
}

TEST_CASE(
    "sloped terrain uses exact face-plane clearance and normal",
    "[physics][sphere][terrain][slope][normal]")
{
    using namespace shark;

    const auto surface = make_ramp_surface();
    constexpr physics::SphereCollider collider{1.0F};
    physics::BallisticBodyState state{
        .position = {0.25F, 0.5F, 0.75F},
        .linear_velocity = {0.0F, -1.0F, 2.0F},
    };
    const auto result =
        physics::advance_sphere_against_terrain(
            state,
            collider,
            surface,
            {},
            1.0F / 60.0F);
    REQUIRE(result);
    REQUIRE(result.value().contact);
    const auto& contact = *result.value().contact;
    constexpr float inverse_sqrt_two = 0.7071067811865475F;

    require_float3(
        contact.surface.position,
        {0.25F, 0.25F, 0.78333336F});
    require_float3(
        contact.surface.normal,
        {-inverse_sqrt_two, inverse_sqrt_two, 0.0F});
    REQUIRE(
        center_plane_distance(state, contact.surface) ==
        Catch::Approx(collider.radius).margin(0.000001));
    REQUIRE(
        state.position.y ==
        Catch::Approx(
            contact.surface.position.y +
            collider.radius / contact.surface.normal.y)
            .margin(0.000001));
    REQUIRE(state.linear_velocity == math::Float3{});
}

TEST_CASE(
    "sphere support has a bounded contact tolerance",
    "[physics][sphere][terrain][tolerance]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    constexpr physics::SphereCollider collider{1.0F};
    const auto threshold_center_y = 1.00001F;
    physics::BallisticBodyState just_inside{
        .position = {
            1.0F,
            std::nextafter(threshold_center_y, 1.0F),
            1.0F,
        },
    };
    const auto inside_result =
        physics::advance_sphere_against_terrain(
            just_inside,
            collider,
            surface,
            {},
            1.0F / 60.0F);
    REQUIRE(inside_result);
    REQUIRE(inside_result.value().contact);
    REQUIRE(
        inside_result.value()
            .contact->plane_separation_before_resolution >
        0.0F);
    REQUIRE(
        inside_result.value().contact->penetration_depth ==
        0.0F);
    require_float3(just_inside.position, {1.0F, 1.0F, 1.0F});

    physics::BallisticBodyState just_outside{
        .position = {
            1.0F,
            std::nextafter(
                threshold_center_y,
                std::numeric_limits<float>::infinity()),
            1.0F,
        },
    };
    const auto outside_before = just_outside;
    const auto outside_result =
        physics::advance_sphere_against_terrain(
            just_outside,
            collider,
            surface,
            {},
            1.0F / 60.0F);
    REQUIRE(outside_result);
    REQUIRE_FALSE(outside_result.value().contact);
    REQUIRE(just_outside == outside_before);
}

TEST_CASE(
    "triangle ties and tile boundaries retain canonical ownership",
    "[physics][sphere][terrain][boundary][normal]")
{
    using namespace shark;

    constexpr physics::SphereCollider collider{1.0F};
    constexpr float inverse_sqrt_two = 0.7071067811865475F;
    const auto twisted = make_twisted_surface();

    physics::BallisticBodyState diagonal{
        .position = {0.5F, 0.5F, 0.5F},
    };
    const auto diagonal_result =
        physics::advance_sphere_against_terrain(
            diagonal,
            collider,
            twisted,
            {},
            1.0F / 60.0F);
    REQUIRE(diagonal_result);
    REQUIRE(diagonal_result.value().contact);
    REQUIRE(
        diagonal_result.value().contact->surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        diagonal_result.value().contact->surface.normal,
        {-inverse_sqrt_two, inverse_sqrt_two, 0.0F});

    physics::BallisticBodyState across_diagonal{
        .position = {0.5001F, 0.5F, 0.4999F},
    };
    const auto across_result =
        physics::advance_sphere_against_terrain(
            across_diagonal,
            collider,
            twisted,
            {},
            1.0F / 60.0F);
    REQUIRE(across_result);
    REQUIRE(across_result.value().contact);
    REQUIRE(
        across_result.value().contact->surface.triangle ==
        terrain::HeightTileTriangle::v00_v11_v10);
    require_float3(
        across_result.value().contact->surface.normal,
        {0.0F, inverse_sqrt_two, -inverse_sqrt_two});

    const auto flat = make_flat_surface();
    physics::BallisticBodyState maximum_edge{
        .position = {2.0F, 0.0F, 1.0F},
    };
    const auto edge_result =
        physics::advance_sphere_against_terrain(
            maximum_edge,
            collider,
            flat,
            {},
            1.0F / 60.0F);
    REQUIRE(edge_result);
    REQUIRE(edge_result.value().contact);
    REQUIRE(edge_result.value().contact->surface.cell_x == 1);
    REQUIRE(edge_result.value().contact->surface.cell_z == 1);

    physics::BallisticBodyState outside{
        .position = {
            std::nextafter(
                2.0F,
                std::numeric_limits<float>::infinity()),
            5.0F,
            1.0F,
        },
        .linear_velocity = {0.0F, 1.0F, 0.0F},
    };
    const auto outside_before = outside;
    auto expected_outside = outside;
    REQUIRE(physics::advance_ballistic_body(
        expected_outside,
        physics::standard_gravity,
        0.1F));
    const auto outside_result =
        physics::advance_sphere_against_terrain(
            outside,
            collider,
            flat,
            physics::standard_gravity,
            0.1F);
    REQUIRE(outside_result);
    REQUIRE_FALSE(outside_result.value().contact);
    REQUIRE(outside != outside_before);
    REQUIRE(outside == expected_outside);
}

TEST_CASE(
    "separating spheres leave terrain support without stale contact",
    "[physics][sphere][terrain][transition]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    constexpr physics::SphereCollider collider{1.0F};
    physics::BallisticBodyState state{
        .position = {1.0F, 1.0F, 1.0F},
        .linear_velocity = {0.0F, 1.0F, 0.0F},
    };
    const auto result =
        physics::advance_sphere_against_terrain(
            state,
            collider,
            surface,
            {},
            0.1F);
    REQUIRE(result);
    REQUIRE_FALSE(result.value().contact);
    require_float3(state.position, {1.0F, 1.1F, 1.0F});
    require_float3(state.linear_velocity, {0.0F, 1.0F, 0.0F});
}

TEST_CASE(
    "sphere terrain contact rejects invalid input transactionally",
    "[physics][sphere][terrain][validation]")
{
    using namespace shark;

    const auto surface = make_flat_surface();
    const physics::BallisticBodyState baseline{
        .position = {1.0F, 3.0F, 1.0F},
        .linear_velocity = {0.0F, -1.0F, 0.0F},
    };
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    const std::array invalid_colliders{
        physics::SphereCollider{0.0F},
        physics::SphereCollider{-1.0F},
        physics::SphereCollider{nan},
        physics::SphereCollider{infinity},
    };

    for (const auto collider : invalid_colliders) {
        CAPTURE(collider.radius);
        REQUIRE_FALSE(physics::is_valid(collider));
        auto state = baseline;
        const auto result =
            physics::advance_sphere_against_terrain(
                state,
                collider,
                surface,
                physics::standard_gravity,
                1.0F / 60.0F);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == baseline);
    }

    constexpr physics::SphereCollider valid_collider{1.0F};
    REQUIRE(physics::is_valid(valid_collider));

    auto invalid_state = baseline;
    invalid_state.position.y = infinity;
    const auto invalid_state_before = invalid_state;
    const auto invalid_state_result =
        physics::advance_sphere_against_terrain(
            invalid_state,
            valid_collider,
            surface,
            physics::standard_gravity,
            1.0F / 60.0F);
    REQUIRE_FALSE(invalid_state_result);
    REQUIRE(
        invalid_state_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(invalid_state == invalid_state_before);

    auto invalid_delta_state = baseline;
    const auto invalid_delta_result =
        physics::advance_sphere_against_terrain(
            invalid_delta_state,
            valid_collider,
            surface,
            physics::standard_gravity,
            0.0F);
    REQUIRE_FALSE(invalid_delta_result);
    REQUIRE(
        invalid_delta_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(invalid_delta_state == baseline);

    physics::BallisticBodyState overflowing{
        .position = {
            1.0F,
            -std::numeric_limits<float>::max(),
            1.0F,
        },
    };
    const auto overflowing_before = overflowing;
    const auto overflow_result =
        physics::advance_sphere_against_terrain(
            overflowing,
            physics::SphereCollider{
                std::numeric_limits<float>::max()},
            surface,
            {},
            0.1F);
    REQUIRE_FALSE(overflow_result);
    REQUIRE(
        overflow_result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(overflowing == overflowing_before);
}

TEST_CASE(
    "Environment Lab proof sphere reaches its fixed support sample",
    "[physics][sphere][terrain][environment-lab][integration]")
{
    using namespace shark;

    auto scenario_result =
        world::make_environment_lab_scenario();
    REQUIRE(scenario_result);
    auto scenario = std::move(scenario_result).value();
    auto surface_result =
        terrain::HeightTileSurface::create(
            std::move(scenario.terrain));
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto support_sample =
        surface.sample_lod0_surface(
            scenario.ballistic_body_spawn_position.x,
            scenario.ballistic_body_spawn_position.z);
    REQUIRE(support_sample);

    physics::BallisticBodyState state{
        .position =
            scenario.ballistic_body_spawn_position,
    };
    const physics::SphereCollider collider{
        scenario.ballistic_body_radius,
    };
    std::optional<physics::SphereTerrainContact> contact;
    constexpr std::uint32_t maximum_ticks = 180;
    for (std::uint32_t tick = 0;
         tick < maximum_ticks;
         ++tick) {
        const auto result =
            physics::advance_sphere_against_terrain(
                state,
                collider,
                surface,
                physics::standard_gravity,
                1.0F / 60.0F);
        REQUIRE(result);
        contact = result.value().contact;
    }

    REQUIRE(contact);
    REQUIRE(contact->surface == *support_sample);
    REQUIRE(state.position.x ==
        scenario.ballistic_body_spawn_position.x);
    REQUIRE(state.position.z ==
        scenario.ballistic_body_spawn_position.z);
    REQUIRE(state.linear_velocity == math::Float3{});
    REQUIRE(
        center_plane_distance(state, contact->surface) ==
        Catch::Approx(collider.radius).margin(0.000001));
}
