#include <shark/physics/sphere_body_collision.hpp>
#include <shark/physics/sphere_terrain_contact.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/world/environment_lab_scenario.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace {

constexpr float comparison_margin = 0.00001F;
constexpr shark::physics::SphereBodyCollisionSettings test_settings{
    .restitution = 0.5F,
};

[[nodiscard]] shark::physics::SphereColliders unit_colliders()
{
    shark::physics::SphereColliders result{};
    result.fill(shark::physics::SphereCollider{1.0F});
    return result;
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

struct CollisionRun final {
    shark::physics::SphereBodyStates states;
    std::uint64_t step_count{};
    std::uint64_t contact_count{};
    std::uint64_t tested_pair_count{};
};

[[nodiscard]] CollisionRun run_collision_schedule(
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
    physics::SphereBodyStates states{};
    states[0] = {
        .position = {-2.0F, 20.0F, 0.0F},
        .linear_velocity = {2.0F, 0.0F, 0.0F},
    };
    states[1] = {
        .position = {2.0F, 20.0F, 0.0F},
        .linear_velocity = {-2.0F, 0.0F, 0.0F},
    };
    const auto colliders = unit_colliders();
    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;
    std::uint64_t contact_count = 0;
    std::uint64_t tested_pair_count = 0;

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
            REQUIRE(physics::advance_ballistic_body(
                states[0],
                {},
                clock.fixed_delta_seconds()));
            REQUIRE(physics::advance_ballistic_body(
                states[1],
                {},
                clock.fixed_delta_seconds()));
            const auto collision_result =
                physics::resolve_sphere_body_collisions(
                    states,
                    colliders,
                    2,
                    test_settings);
            REQUIRE(collision_result);
            contact_count +=
                collision_result.value().contact_count;
            tested_pair_count +=
                collision_result.value().tested_pair_count;
        }
    }

    return CollisionRun{
        .states = states,
        .step_count = clock.total_step_count(),
        .contact_count = contact_count,
        .tested_pair_count = tested_pair_count,
    };
}

} // namespace

TEST_CASE(
    "sphere body capacity and brute-force pair count are fixed",
    "[physics][sphere][body-collision][capacity][pairs]")
{
    using namespace shark::physics;

    STATIC_REQUIRE(sphere_body_capacity == 4);
    STATIC_REQUIRE(sphere_pair_capacity == 6);
    STATIC_REQUIRE(
        std::tuple_size_v<SphereBodyStates> ==
        sphere_body_capacity);
    STATIC_REQUIRE(
        std::tuple_size_v<SphereColliders> ==
        sphere_body_capacity);

    SphereBodyStates states{};
    states[0].position = {-30.0F, 0.0F, 0.0F};
    states[1].position = {-10.0F, 0.0F, 0.0F};
    states[2].position = {10.0F, 0.0F, 0.0F};
    states[3].position = {30.0F, 0.0F, 0.0F};
    const auto before = states;
    const auto colliders = unit_colliders();

    for (const auto body_count : {0U, 1U, 2U, 3U, 4U}) {
        CAPTURE(body_count);
        states = before;
        const auto result = resolve_sphere_body_collisions(
            states,
            colliders,
            body_count,
            test_settings);
        REQUIRE(result);
        REQUIRE(
            result.value().tested_pair_count ==
            body_count * (body_count - (body_count > 0U ? 1U : 0U)) /
                2U);
        REQUIRE(result.value().contact_count == 0);
        REQUIRE(states == before);
    }
}

TEST_CASE(
    "separated spheres are an exact no-op",
    "[physics][sphere][body-collision][separated]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {2.0F, 1.0F, -3.0F},
    };
    states[1] = {
        .position = {2.01F, 0.0F, 0.0F},
        .linear_velocity = {-4.0F, -2.0F, 5.0F},
    };
    const auto before = states;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().tested_pair_count == 1);
    REQUIRE(result.value().contact_count == 0);
    REQUIRE(states == before);
}

TEST_CASE(
    "stationary sphere overlap is split equally",
    "[physics][sphere][body-collision][overlap]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0].position = {0.0F, 0.0F, 0.0F};
    states[1].position = {1.5F, 0.0F, 0.0F};
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().tested_pair_count == 1);
    REQUIRE(result.value().contact_count == 1);
    const auto& contact = result.value().contacts[0];
    REQUIRE(contact.first_body_index == 0);
    REQUIRE(contact.second_body_index == 1);
    require_float3(contact.normal, {1.0F, 0.0F, 0.0F});
    REQUIRE(
        contact.penetration_depth ==
        Catch::Approx(0.5F).margin(0.000001F));
    REQUIRE(
        contact.relative_normal_velocity_before_resolution ==
        0.0F);
    REQUIRE(contact.normal_impulse_magnitude == 0.0F);
    require_float3(states[0].position, {-0.25F, 0.0F, 0.0F});
    require_float3(states[1].position, {1.75F, 0.0F, 0.0F});
    REQUIRE(states[0].linear_velocity == shark::math::Float3{});
    REQUIRE(states[1].linear_velocity == shark::math::Float3{});
    REQUIRE(
        states[1].position.x - states[0].position.x ==
        Catch::Approx(2.0F).margin(0.000001F));
}

TEST_CASE(
    "exactly touching approaching spheres receive an impulse",
    "[physics][sphere][body-collision][touching][impulse]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {1.0F, 0.0F, 0.0F},
    };
    states[1] = {
        .position = {2.0F, 0.0F, 0.0F},
        .linear_velocity = {-1.0F, 0.0F, 0.0F},
    };
    const auto positions_before = states;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().contact_count == 1);
    const auto& contact = result.value().contacts[0];
    REQUIRE(contact.penetration_depth == 0.0F);
    REQUIRE(
        contact.relative_normal_velocity_before_resolution ==
        Catch::Approx(-2.0F).margin(0.000001F));
    REQUIRE(
        contact.normal_impulse_magnitude ==
        Catch::Approx(1.5F).margin(0.000001F));
    REQUIRE(states[0].position == positions_before[0].position);
    REQUIRE(states[1].position == positions_before[1].position);
    require_float3(
        states[0].linear_velocity,
        {-0.5F, 0.0F, 0.0F});
    require_float3(
        states[1].linear_velocity,
        {0.5F, 0.0F, 0.0F});
}

TEST_CASE(
    "head-on spheres apply equal-mass restitution",
    "[physics][sphere][body-collision][impulse][restitution]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {2.0F, 0.0F, 0.0F},
    };
    states[1] = {
        .position = {1.5F, 0.0F, 0.0F},
        .linear_velocity = {-2.0F, 0.0F, 0.0F},
    };
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().contact_count == 1);
    const auto& contact = result.value().contacts[0];
    REQUIRE(
        contact.relative_normal_velocity_before_resolution ==
        Catch::Approx(-4.0F).margin(0.000001F));
    REQUIRE(
        contact.normal_impulse_magnitude ==
        Catch::Approx(3.0F).margin(0.000001F));
    require_float3(states[0].linear_velocity, {-1.0F, 0.0F, 0.0F});
    require_float3(states[1].linear_velocity, {1.0F, 0.0F, 0.0F});
    REQUIRE(
        states[0].linear_velocity.x +
            states[1].linear_velocity.x ==
        Catch::Approx(0.0F).margin(0.000001F));
    REQUIRE(
        states[1].linear_velocity.x -
            states[0].linear_velocity.x ==
        Catch::Approx(2.0F).margin(0.000001F));
}

TEST_CASE(
    "unequal normal speeds preserve momentum and tangential velocity",
    "[physics][sphere][body-collision][impulse][tangent]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {4.0F, 1.0F, 2.0F},
    };
    states[1] = {
        .position = {1.5F, 0.0F, 0.0F},
        .linear_velocity = {-2.0F, -3.0F, 5.0F},
    };
    const auto normal_momentum_before =
        states[0].linear_velocity.x +
        states[1].linear_velocity.x;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().contact_count == 1);
    const auto& contact = result.value().contacts[0];
    REQUIRE(
        contact.relative_normal_velocity_before_resolution ==
        Catch::Approx(-6.0F).margin(0.000001F));
    REQUIRE(
        contact.normal_impulse_magnitude ==
        Catch::Approx(4.5F).margin(0.000001F));
    require_float3(
        states[0].linear_velocity,
        {-0.5F, 1.0F, 2.0F});
    require_float3(
        states[1].linear_velocity,
        {2.5F, -3.0F, 5.0F});
    REQUIRE(
        states[0].linear_velocity.x +
            states[1].linear_velocity.x ==
        Catch::Approx(normal_momentum_before)
            .margin(0.000001F));
    REQUIRE(
        states[1].linear_velocity.x -
            states[0].linear_velocity.x ==
        Catch::Approx(3.0F).margin(0.000001F));
}

TEST_CASE(
    "separating overlap corrects position without an impulse",
    "[physics][sphere][body-collision][separating]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {-1.0F, 2.0F, 3.0F},
    };
    states[1] = {
        .position = {1.5F, 0.0F, 0.0F},
        .linear_velocity = {1.0F, -4.0F, 5.0F},
    };
    const auto first_velocity = states[0].linear_velocity;
    const auto second_velocity = states[1].linear_velocity;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().contact_count == 1);
    REQUIRE(
        result.value()
            .contacts[0]
            .relative_normal_velocity_before_resolution ==
        Catch::Approx(2.0F).margin(0.000001F));
    REQUIRE(
        result.value().contacts[0].normal_impulse_magnitude ==
        0.0F);
    require_float3(states[0].position, {-0.25F, 0.0F, 0.0F});
    require_float3(states[1].position, {1.75F, 0.0F, 0.0F});
    REQUIRE(states[0].linear_velocity == first_velocity);
    REQUIRE(states[1].linear_velocity == second_velocity);
}

TEST_CASE(
    "coincident sphere centers use a finite stable fallback",
    "[physics][sphere][body-collision][coincident][determinism]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0].position = {4.0F, 5.0F, 6.0F};
    states[1].position = states[0].position;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        2,
        test_settings);

    REQUIRE(result);
    REQUIRE(result.value().contact_count == 1);
    require_float3(
        result.value().contacts[0].normal,
        {1.0F, 0.0F, 0.0F});
    require_float3(states[0].position, {3.0F, 5.0F, 6.0F});
    require_float3(states[1].position, {5.0F, 5.0F, 6.0F});
    REQUIRE(shark::math::is_finite(states[0].position));
    REQUIRE(shark::math::is_finite(states[1].position));

    SphereBodyStates repeated{};
    repeated[0].position = {4.0F, 5.0F, 6.0F};
    repeated[1].position = repeated[0].position;
    const auto repeated_result = resolve_sphere_body_collisions(
        repeated,
        unit_colliders(),
        2,
        test_settings);
    REQUIRE(repeated_result);
    REQUIRE(repeated == states);
}

TEST_CASE(
    "sphere contacts are published in stable lexicographic order",
    "[physics][sphere][body-collision][pairs][order]")
{
    using namespace shark::physics;

    SphereBodyStates states{};
    states[0].position = {-10.0F, 0.0F, 0.0F};
    states[2].position = {-8.5F, 0.0F, 0.0F};
    states[1].position = {10.0F, 0.0F, 0.0F};
    states[3].position = {11.5F, 0.0F, 0.0F};
    const auto initial_states = states;
    const auto result = resolve_sphere_body_collisions(
        states,
        unit_colliders(),
        sphere_body_capacity,
        test_settings);

    REQUIRE(result);
    REQUIRE(
        result.value().tested_pair_count ==
        sphere_pair_capacity);
    REQUIRE(result.value().contact_count == 2);
    REQUIRE(
        result.value().contacts[0].first_body_index ==
        0);
    REQUIRE(
        result.value().contacts[0].second_body_index ==
        2);
    REQUIRE(
        result.value().contacts[1].first_body_index ==
        1);
    REQUIRE(
        result.value().contacts[1].second_body_index ==
        3);

    auto repeated_states = initial_states;
    const auto repeated_result = resolve_sphere_body_collisions(
        repeated_states,
        unit_colliders(),
        sphere_body_capacity,
        test_settings);
    REQUIRE(repeated_result);
    REQUIRE(repeated_result.value() == result.value());
    REQUIRE(repeated_states == states);
}

TEST_CASE(
    "sphere body collision rejects invalid input transactionally",
    "[physics][sphere][body-collision][validation]")
{
    using namespace shark;

    physics::SphereBodyStates baseline{};
    baseline[0] = {
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {2.0F, 0.0F, 0.0F},
    };
    baseline[1] = {
        .position = {1.5F, 0.0F, 0.0F},
        .linear_velocity = {-2.0F, 0.0F, 0.0F},
    };
    auto colliders = unit_colliders();

    SECTION("body count exceeds fixed capacity")
    {
        auto states = baseline;
        const auto result =
            physics::resolve_sphere_body_collisions(
                states,
                colliders,
                physics::sphere_body_capacity + 1U,
                test_settings);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(states == baseline);
    }

    SECTION("restitution is outside the closed unit interval")
    {
        constexpr auto nan =
            std::numeric_limits<float>::quiet_NaN();
        constexpr auto infinity =
            std::numeric_limits<float>::infinity();
        for (const auto restitution :
             {-0.001F, 1.001F, nan, infinity}) {
            CAPTURE(restitution);
            auto states = baseline;
            const auto result =
                physics::resolve_sphere_body_collisions(
                    states,
                    colliders,
                    2,
                    physics::SphereBodyCollisionSettings{
                        .restitution = restitution,
                    });
            REQUIRE_FALSE(result);
            REQUIRE(
                result.error().code() ==
                core::ErrorCode::invalid_argument);
            REQUIRE(states == baseline);
        }
    }

    SECTION("active state is nonfinite")
    {
        auto states = baseline;
        states[1].position.x =
            std::numeric_limits<float>::infinity();
        const auto before = states;
        const auto result =
            physics::resolve_sphere_body_collisions(
                states,
                colliders,
                2,
                test_settings);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(states == before);
    }

    SECTION("active collider is invalid")
    {
        constexpr auto nan =
            std::numeric_limits<float>::quiet_NaN();
        constexpr auto infinity =
            std::numeric_limits<float>::infinity();
        for (const auto radius : {0.0F, -1.0F, nan, infinity}) {
            CAPTURE(radius);
            colliders[1].radius = radius;
            auto states = baseline;
            const auto result =
                physics::resolve_sphere_body_collisions(
                    states,
                    colliders,
                    2,
                    test_settings);
            REQUIRE_FALSE(result);
            REQUIRE(
                result.error().code() ==
                core::ErrorCode::invalid_argument);
            REQUIRE(states == baseline);
        }
    }

    SECTION("position correction exceeds float range")
    {
        const auto maximum =
            std::numeric_limits<float>::max();
        physics::SphereBodyStates states{};
        states[0].position = {maximum, 0.0F, 0.0F};
        states[1].position = {maximum * 0.5F, 0.0F, 0.0F};
        const auto before = states;
        colliders[0].radius = maximum;
        colliders[1].radius = maximum;
        const auto result =
            physics::resolve_sphere_body_collisions(
                states,
                colliders,
                2,
                test_settings);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(states == before);
    }
}

TEST_CASE(
    "sphere collision outcome is invariant across render partitions",
    "[physics][sphere][body-collision][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };
    const auto baseline =
        run_collision_schedule(render_rates[0], 2);
    REQUIRE(baseline.step_count == 120);
    REQUIRE(baseline.contact_count == 1);
    REQUIRE(baseline.tested_pair_count == 120);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run =
            run_collision_schedule(render_rate, 2);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.contact_count == baseline.contact_count);
        REQUIRE(
            run.tested_pair_count ==
            baseline.tested_pair_count);
        REQUIRE(run.states == baseline.states);
    }
}

TEST_CASE(
    "Environment Lab pair collides airborne while primary sphere rests",
    "[physics][sphere][body-collision][terrain][environment-lab]"
    "[integration]")
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

    physics::SphereBodyStates states{};
    physics::SphereColliders colliders{};
    for (std::size_t body_index = 0;
         body_index < world::environment_lab_sphere_body_count;
         ++body_index) {
        states[body_index] = physics::BallisticBodyState{
            .position =
                scenario.sphere_body_spawn_positions[body_index],
            .linear_velocity =
                scenario.sphere_body_initial_velocities[body_index],
        };
        colliders[body_index] =
            physics::SphereCollider{
                .radius = scenario.sphere_body_radius,
            };
    }

    std::array<
        std::optional<physics::SphereTerrainContact>,
        physics::sphere_body_capacity>
        terrain_contacts{};
    bool observed_airborne_pair_impulse = false;
    std::uint64_t pair_contact_count = 0;
    constexpr std::uint32_t tick_count = 180;
    for (std::uint32_t tick = 0;
         tick < tick_count;
         ++tick) {
        for (std::size_t body_index = 0;
             body_index < world::environment_lab_sphere_body_count;
             ++body_index) {
            const auto terrain_result =
                physics::advance_sphere_against_terrain(
                    states[body_index],
                    colliders[body_index],
                    surface,
                    physics::standard_gravity,
                    1.0F / 60.0F);
            REQUIRE(terrain_result);
            terrain_contacts[body_index] =
                terrain_result.value().contact;
        }
        const auto collision_result =
            physics::resolve_sphere_body_collisions(
                states,
                colliders,
                world::environment_lab_sphere_body_count,
                physics::SphereBodyCollisionSettings{
                    .restitution =
                        scenario.sphere_restitution,
                });
        REQUIRE(collision_result);
        pair_contact_count +=
            collision_result.value().contact_count;
        for (std::size_t contact_index = 0;
             contact_index <
                collision_result.value().contact_count;
             ++contact_index) {
            const auto& contact =
                collision_result.value().contacts[contact_index];
            if (contact.first_body_index == 1 &&
                contact.second_body_index == 2 &&
                contact.normal_impulse_magnitude > 0.0F) {
                REQUIRE_FALSE(
                    terrain_contacts[1].has_value());
                REQUIRE_FALSE(
                    terrain_contacts[2].has_value());
                REQUIRE(contact.normal ==
                    math::Float3{1.0F, 0.0F, 0.0F});
                REQUIRE(
                    contact
                        .relative_normal_velocity_before_resolution ==
                    Catch::Approx(-8.0F).margin(0.000001F));
                REQUIRE(
                    contact.normal_impulse_magnitude ==
                    Catch::Approx(7.0F).margin(0.000001F));
                REQUIRE(states[1].linear_velocity.x ==
                    Catch::Approx(-2.0F).margin(0.000001F));
                REQUIRE(states[2].linear_velocity.x ==
                    Catch::Approx(4.0F).margin(0.000001F));
                observed_airborne_pair_impulse = true;
            }
        }
    }

    REQUIRE(observed_airborne_pair_impulse);
    REQUIRE(pair_contact_count >= 1);
    REQUIRE(terrain_contacts[0]);
    REQUIRE(states[0].linear_velocity == math::Float3{});
    REQUIRE(states[0].position.x ==
        scenario.sphere_body_spawn_positions[0].x);
    REQUIRE(states[0].position.z ==
        scenario.sphere_body_spawn_positions[0].z);
}
