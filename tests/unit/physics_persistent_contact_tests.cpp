#include <shark/physics/persistent_contact.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

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

[[nodiscard]] float length(
    const shark::math::Float3 value) noexcept
{
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
}

[[nodiscard]] shark::physics::RigidBodyState body_at(
    const shark::math::Float3 position = {},
    const shark::math::Float3 linear_velocity = {})
{
    return shark::physics::RigidBodyState{
        .position = position,
        .linear_velocity = linear_velocity,
    };
}

[[nodiscard]] shark::physics::ContactBodyMassProperties mass(
    const float inverse_mass = 1.0F,
    const shark::math::Float3 inverse_inertia = {})
{
    return shark::physics::ContactBodyMassProperties{
        .inverse_mass = inverse_mass,
        .local_inverse_inertia = inverse_inertia,
    };
}

[[nodiscard]] shark::physics::ContactConstraint constraint(
    const std::size_t first_body,
    const std::size_t second_body,
    const shark::math::Float3 normal,
    const shark::math::Float3 position = {},
    const shark::physics::ContactMaterial material = {})
{
    return shark::physics::ContactConstraint{
        .first_body_index = first_body,
        .second_body_index = second_body,
        .normal = normal,
        .material = material,
        .points = {{
            shark::physics::ContactConstraintPoint{
                .position = position,
            },
        }},
        .point_count = 1U,
    };
}

[[nodiscard]] shark::physics::ContactManifoldIdentity identity(
    const std::uint64_t first,
    const std::uint64_t second,
    const std::uint32_t first_shape = 0U,
    const std::uint32_t second_shape = 0U)
{
    return shark::physics::ContactManifoldIdentity{
        .first_endpoint = first,
        .second_endpoint = second,
        .first_shape = first_shape,
        .second_shape = second_shape,
    };
}

[[nodiscard]] shark::physics::ContactPersistenceDescriptor descriptor(
    const shark::physics::ContactManifoldIdentity manifold_identity,
    const shark::math::Float3 point_on_first = {},
    const shark::math::Float3 point_on_second = {})
{
    return shark::physics::ContactPersistenceDescriptor{
        .identity = manifold_identity,
        .points = {{
            shark::physics::ContactPersistencePoint{
                .point_on_first = point_on_first,
                .point_on_second = point_on_second,
            },
        }},
        .point_count = 1U,
    };
}

[[nodiscard]] shark::physics::ContactManifoldCache one_point_cache(
    const shark::physics::ContactManifoldIdentity manifold_identity,
    const shark::math::Float3 normal,
    const shark::math::Float3 first_anchor,
    const shark::math::Float3 second_anchor,
    const float normal_impulse,
    const shark::math::Float3 tangent_impulse,
    const std::uint64_t tick,
    const bool first_static = false,
    const bool second_static = false)
{
    shark::physics::ContactManifoldCache cache{
        .manifold_count = 1U,
        .last_committed_tick = tick,
        .has_committed_tick = true,
    };
    cache.manifolds[0] = shark::physics::CachedContactManifold{
        .identity = manifold_identity,
        .normal = normal,
        .points = {{
            shark::physics::CachedContactPoint{
                .first_anchor = first_anchor,
                .second_anchor = second_anchor,
                .normal_impulse_magnitude = normal_impulse,
                .tangent_impulse = tangent_impulse,
            },
        }},
        .point_count = 1U,
        .last_seen_tick = tick,
        .first_endpoint_static = first_static,
        .second_endpoint_static = second_static,
    };
    return cache;
}

template<std::size_t StateCount,
         std::size_t MassCount,
         std::size_t ConstraintCount,
         std::size_t DescriptorCount>
void require_persistent_failure_is_transactional(
    std::array<shark::physics::RigidBodyState, StateCount> states,
    const std::array<
        shark::physics::ContactBodyMassProperties,
        MassCount>& masses,
    const std::array<
        shark::physics::ContactConstraint,
        ConstraintCount>& constraints,
    const std::array<
        shark::physics::ContactPersistenceDescriptor,
        DescriptorCount>& descriptors,
    const std::uint64_t tick,
    shark::physics::ContactManifoldCache cache,
    const shark::core::ErrorCode expected_code =
        shark::core::ErrorCode::invalid_argument,
    const shark::physics::ContactPersistenceSettings settings = {})
{
    const auto states_before = states;
    const auto cache_before = cache;
    const auto result =
        shark::physics::solve_persistent_contact_constraints(
            states,
            masses,
            constraints,
            descriptors,
            tick,
            cache,
            {},
            settings);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() == expected_code);
    REQUIRE(states == states_before);
    REQUIRE(
        std::memcmp(&cache, &cache_before, sizeof(cache)) == 0);
}

} // namespace

TEST_CASE(
    "warm starts converge to the cold analytic contact solution",
    "[physics][persistent-contact][warm-start][restitution]")
{
    using namespace shark;

    const std::array initial_states{
        body_at({}, {0.0F, -4.0F, 0.0F}),
    };
    const std::array masses{mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F},
            {},
            physics::ContactMaterial{
                .restitution = 0.5F,
            }),
    };
    auto settings = physics::ContactSolverSettings{};
    settings.velocity_iterations = 1U;
    settings.restitution_velocity_threshold = 0.0F;

    auto cold_states = initial_states;
    const auto cold = physics::solve_contact_constraints(
        cold_states,
        masses,
        constraints,
        settings);
    REQUIRE(cold);
    require_float3(cold_states[0].linear_velocity, {0.0F, 2.0F, 0.0F});
    REQUIRE(
        cold.value().constraints[0].points[0]
                .normal_impulse_magnitude ==
            Catch::Approx(6.0F));

    for (const auto supplied_impulse : {2.0F, 8.0F}) {
        CAPTURE(supplied_impulse);
        auto warm_states = initial_states;
        const std::array warm_starts{
            physics::ContactConstraintWarmStart{
                .points = {{
                    physics::ContactPointWarmStart{
                        .normal_impulse_magnitude = supplied_impulse,
                    },
                }},
                .point_count = 1U,
            },
        };
        const auto warm = physics::solve_contact_constraints(
            warm_states,
            masses,
            constraints,
            warm_starts,
            settings);
        REQUIRE(warm);
        REQUIRE(warm_states == cold_states);
        const auto& impulse = warm.value().constraints[0].points[0];
        REQUIRE(
            impulse.relative_normal_velocity_before_resolution ==
            Catch::Approx(-4.0F));
        REQUIRE(
            impulse.warm_start_normal_impulse_magnitude ==
            Catch::Approx(supplied_impulse));
        REQUIRE(
            impulse.normal_impulse_magnitude ==
            Catch::Approx(6.0F));
    }
}

TEST_CASE(
    "warm tangent impulse is reprojected and clamped to the current cone",
    "[physics][persistent-contact][warm-start][friction][projection]")
{
    using namespace shark;

    constexpr math::Float3 normal{0.6F, 0.8F, 0.0F};
    std::array states{
        body_at({}, {-1.6F, -1.3F, 0.0F}),
    };
    const std::array masses{mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            normal,
            {},
            physics::ContactMaterial{
                .static_friction = 0.25F,
                .dynamic_friction = 0.25F,
            }),
    };
    const std::array warm_starts{
        physics::ContactConstraintWarmStart{
            .points = {{
                physics::ContactPointWarmStart{
                    .normal_impulse_magnitude = 2.0F,
                    .tangent_impulse = {1.0F, 0.0F, 0.0F},
                },
            }},
            .point_count = 1U,
        },
    };
    auto settings = physics::ContactSolverSettings{};
    settings.velocity_iterations = 1U;
    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints,
        warm_starts,
        settings);
    REQUIRE(result);
    const auto& impulse = result.value().constraints[0].points[0];
    REQUIRE(
        impulse.relative_normal_velocity_before_resolution ==
        Catch::Approx(-2.0F).margin(0.000002F));
    REQUIRE(
        impulse.warm_start_normal_impulse_magnitude ==
        Catch::Approx(2.0F));
    require_float3(
        impulse.warm_start_tangent_impulse,
        {0.4F, -0.3F, 0.0F},
        0.000002F);
    REQUIRE(
        impulse.warm_start_tangent_impulse.x * normal.x +
            impulse.warm_start_tangent_impulse.y * normal.y ==
        Catch::Approx(0.0F).margin(0.000002F));
    REQUIRE(
        length(impulse.warm_start_tangent_impulse) ==
        Catch::Approx(0.5F).margin(0.000002F));
    require_float3(states[0].linear_velocity, {}, 0.000002F);
}

TEST_CASE(
    "persistent point matching survives permutation drift and translation",
    "[physics][persistent-contact][identity][points][matching]")
{
    using namespace shark;

    constexpr auto manifold_identity =
        physics::ContactManifoldIdentity{
            .first_endpoint = 100U,
            .second_endpoint = 200U,
            .first_shape = 3U,
            .second_shape = 7U,
        };
    constexpr std::array<float, 4> anchors{-0.75F, -0.25F, 0.25F, 0.75F};
    constexpr std::array<float, 4> impulses{1.0F, 2.0F, 3.0F, 4.0F};
    physics::ContactManifoldCache cache{
        .manifold_count = 1U,
        .last_committed_tick = 40U,
        .has_committed_tick = true,
    };
    auto& cached = cache.manifolds[0];
    cached.identity = manifold_identity;
    cached.normal = {0.0F, 1.0F, 0.0F};
    cached.point_count = anchors.size();
    cached.last_seen_tick = 40U;
    for (std::size_t index = 0U; index < anchors.size(); ++index) {
        cached.points[index] = physics::CachedContactPoint{
            .first_anchor = {anchors[index], 0.0F, 0.0F},
            .second_anchor = {anchors[index], 0.0F, 0.0F},
            .normal_impulse_magnitude = impulses[index],
        };
    }

    std::array states{
        body_at({10.01F, 0.0F, -4.0F}),
        body_at({10.01F, 0.0F, -4.0F}),
    };
    const std::array masses{mass(), mass()};
    physics::ContactConstraint current{
        .first_body_index = 0U,
        .second_body_index = 1U,
        .normal = {0.0F, 1.0F, 0.0F},
        .point_count = anchors.size(),
    };
    physics::ContactPersistenceDescriptor current_descriptor{
        .identity = manifold_identity,
        .point_count = anchors.size(),
    };
    for (std::size_t current_index = 0U;
         current_index < anchors.size();
         ++current_index) {
        const auto cached_index = anchors.size() - 1U - current_index;
        const math::Float3 witness{
            10.01F + anchors[cached_index],
            0.0F,
            -3.995F,
        };
        current.points[current_index] =
            physics::ContactConstraintPoint{
                .position = witness,
            };
        current_descriptor.points[current_index] =
            physics::ContactPersistencePoint{
                .point_on_first = witness,
                .point_on_second = witness,
            };
    }
    const std::array constraints{current};
    const std::array descriptors{current_descriptor};
    auto settings = physics::ContactSolverSettings{};
    settings.velocity_iterations = 1U;
    const auto result =
        physics::solve_persistent_contact_constraints(
            states,
            masses,
            constraints,
            descriptors,
            41U,
            cache,
            settings);
    REQUIRE(result);
    REQUIRE(result.value().warm_started_point_count == 4U);
    REQUIRE(result.value().cold_started_point_count == 0U);
    for (std::size_t current_index = 0U;
         current_index < anchors.size();
         ++current_index) {
        const auto cached_index = anchors.size() - 1U - current_index;
        CAPTURE(current_index, cached_index);
        REQUIRE(
            result.value().solver.constraints[0].points[current_index]
                    .warm_start_normal_impulse_magnitude ==
                Catch::Approx(impulses[cached_index]));
    }
}

TEST_CASE(
    "equidistant point matches use stable current then cached index order",
    "[physics][persistent-contact][points][tie][determinism]")
{
    using namespace shark;

    constexpr auto manifold_identity =
        physics::ContactManifoldIdentity{
            .first_endpoint = 100U,
            .second_endpoint = 200U,
        };
    physics::ContactManifoldCache cache{
        .manifold_count = 1U,
        .last_committed_tick = 1U,
        .has_committed_tick = true,
    };
    cache.manifolds[0] = physics::CachedContactManifold{
        .identity = manifold_identity,
        .normal = {0.0F, 1.0F, 0.0F},
        .points = {{
            physics::CachedContactPoint{
                .first_anchor = {-0.01F, 0.0F, 0.0F},
                .second_anchor = {-0.01F, 0.0F, 0.0F},
                .normal_impulse_magnitude = 11.0F,
            },
            physics::CachedContactPoint{
                .first_anchor = {0.01F, 0.0F, 0.0F},
                .second_anchor = {0.01F, 0.0F, 0.0F},
                .normal_impulse_magnitude = 22.0F,
            },
        }},
        .point_count = 2U,
        .last_seen_tick = 1U,
    };
    std::array states{body_at(), body_at()};
    const std::array masses{mass(), mass()};
    const std::array constraints{
        physics::ContactConstraint{
            .first_body_index = 0U,
            .second_body_index = 1U,
            .normal = {0.0F, 1.0F, 0.0F},
            .points = {{
                physics::ContactConstraintPoint{},
                physics::ContactConstraintPoint{},
            }},
            .point_count = 2U,
        },
    };
    const std::array descriptors{
        physics::ContactPersistenceDescriptor{
            .identity = manifold_identity,
            .points = {{
                physics::ContactPersistencePoint{},
                physics::ContactPersistencePoint{},
            }},
            .point_count = 2U,
        },
    };
    const auto result =
        physics::solve_persistent_contact_constraints(
            states,
            masses,
            constraints,
            descriptors,
            2U,
            cache);
    REQUIRE(result);
    REQUIRE(result.value().warm_started_point_count == 2U);
    REQUIRE(
        result.value().solver.constraints[0].points[0]
                .warm_start_normal_impulse_magnitude ==
            Catch::Approx(11.0F));
    REQUIRE(
        result.value().solver.constraints[0].points[1]
                .warm_start_normal_impulse_magnitude ==
            Catch::Approx(22.0F));
}

TEST_CASE(
    "persistent matching rejects normal distance and generation changes",
    "[physics][persistent-contact][identity][miss]")
{
    using namespace shark;

    constexpr auto original_identity =
        physics::ContactManifoldIdentity{
            .first_endpoint = 10U,
            .second_endpoint = 20U,
        };
    const std::array masses{mass()};

    const auto run = [&](
        const physics::ContactManifoldIdentity current_identity,
        const math::Float3 current_normal,
        const math::Float3 current_witness) {
        std::array states{body_at()};
        auto cache = one_point_cache(
            original_identity,
            {0.0F, 1.0F, 0.0F},
            {},
            {},
            2.0F,
            {},
            1U,
            true,
            false);
        const std::array constraints{
            constraint(
                physics::static_contact_body_index,
                0U,
                current_normal,
                current_witness),
        };
        const std::array descriptors{
            descriptor(
                current_identity,
                current_witness,
                current_witness),
        };
        const auto result =
            physics::solve_persistent_contact_constraints(
                states,
                masses,
                constraints,
                descriptors,
                2U,
                cache);
        REQUIRE(result);
        REQUIRE(result.value().warm_started_point_count == 0U);
        REQUIRE(result.value().cold_started_point_count == 1U);
    };

    SECTION("normal alignment")
    {
        run(original_identity, {1.0F, 0.0F, 0.0F}, {});
    }
    SECTION("anchor distance")
    {
        run(original_identity, {0.0F, 1.0F, 0.0F}, {0.051F, 0.0F, 0.0F});
    }
    SECTION("generation-bearing endpoint")
    {
        run(identity(10U, 21U), {0.0F, 1.0F, 0.0F}, {});
    }
}

TEST_CASE(
    "persistent cache retention expiry and reuse are explicit",
    "[physics][persistent-contact][cache][expiry][reuse]")
{
    using namespace shark;

    auto cache = one_point_cache(
        identity(10U, 20U),
        {0.0F, 1.0F, 0.0F},
        {},
        {},
        3.0F,
        {},
        10U,
        true,
        false);
    std::array<physics::RigidBodyState, 0> no_states{};
    constexpr std::array<physics::ContactBodyMassProperties, 0> no_masses{};
    constexpr std::array<physics::ContactConstraint, 0> no_constraints{};
    constexpr std::array<physics::ContactPersistenceDescriptor, 0>
        no_descriptors{};

    const auto retained = physics::solve_persistent_contact_constraints(
        no_states,
        no_masses,
        no_constraints,
        no_descriptors,
        11U,
        cache);
    REQUIRE(retained);
    REQUIRE(retained.value().expired_manifold_count == 0U);
    REQUIRE(cache.manifold_count == 1U);

    SECTION("reappearance after one empty tick remains eligible")
    {
        auto reappearance_cache = cache;
        std::array states{body_at({}, {0.0F, -3.0F, 0.0F})};
        const std::array masses{mass()};
        const std::array constraints{
            constraint(
                physics::static_contact_body_index,
                0U,
                {0.0F, 1.0F, 0.0F}),
        };
        const std::array descriptors{
            descriptor(identity(10U, 20U)),
        };
        const auto reappeared =
            physics::solve_persistent_contact_constraints(
                states,
                masses,
                constraints,
                descriptors,
                12U,
                reappearance_cache);
        REQUIRE(reappeared);
        REQUIRE(reappeared.value().warm_started_point_count == 1U);
        REQUIRE(
            reappeared.value().solver.constraints[0].points[0]
                    .warm_start_normal_impulse_magnitude ==
                Catch::Approx(3.0F));
    }

    const auto expired = physics::solve_persistent_contact_constraints(
        no_states,
        no_masses,
        no_constraints,
        no_descriptors,
        12U,
        cache);
    REQUIRE(expired);
    REQUIRE(expired.value().expired_manifold_count == 1U);
    REQUIRE(cache.manifold_count == 0U);

    std::array states{body_at({}, {0.0F, -1.0F, 0.0F})};
    const std::array masses{mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
    };
    const std::array descriptors{
        descriptor(identity(10U, 21U)),
    };
    const auto reused = physics::solve_persistent_contact_constraints(
        states,
        masses,
        constraints,
        descriptors,
        13U,
        cache);
    REQUIRE(reused);
    REQUIRE(reused.value().warm_started_point_count == 0U);
    REQUIRE(reused.value().cold_started_point_count == 1U);
    REQUIRE(cache.manifold_count == 1U);
    REQUIRE(cache.manifolds[0].identity == identity(10U, 21U));
}

TEST_CASE(
    "persistent cache is sorted while solver order remains caller-owned",
    "[physics][persistent-contact][cache][order][determinism]")
{
    using namespace shark;

    const std::array initial_states{
        body_at({}, {1.0F, 0.0F, 0.0F}),
        body_at({1.0F, 0.0F, 0.0F}),
        body_at({2.0F, 0.0F, 0.0F}),
    };
    const std::array masses{mass(), mass(), mass()};
    const std::array constraints{
        constraint(1U, 2U, {1.0F, 0.0F, 0.0F}, {1.5F, 0.0F, 0.0F}),
        constraint(0U, 1U, {1.0F, 0.0F, 0.0F}, {0.5F, 0.0F, 0.0F}),
    };
    const std::array descriptors{
        descriptor(
            identity(200U, 300U),
            {1.5F, 0.0F, 0.0F},
            {1.5F, 0.0F, 0.0F}),
        descriptor(
            identity(100U, 200U),
            {0.5F, 0.0F, 0.0F},
            {0.5F, 0.0F, 0.0F}),
    };
    auto solver_settings = physics::ContactSolverSettings{};
    solver_settings.velocity_iterations = 1U;

    const auto run = [&]() {
        auto states = initial_states;
        physics::ContactManifoldCache cache{};
        const auto result =
            physics::solve_persistent_contact_constraints(
                states,
                masses,
                constraints,
                descriptors,
                1U,
                cache,
                solver_settings);
        REQUIRE(result);
        return std::pair{
            std::pair{states, cache},
            result.value(),
        };
    };

    const auto baseline = run();
    REQUIRE(baseline.first.second.manifold_count == 2U);
    REQUIRE(
        baseline.first.second.manifolds[0].identity ==
        identity(100U, 200U));
    REQUIRE(
        baseline.first.second.manifolds[1].identity ==
        identity(200U, 300U));
    REQUIRE(
        baseline.second.solver.constraints[0].points[0]
                .normal_impulse_magnitude ==
            0.0F);
    REQUIRE(
        baseline.second.solver.constraints[1].points[0]
                .normal_impulse_magnitude ==
            Catch::Approx(0.5F));

    for (std::size_t repeat = 0U; repeat < 64U; ++repeat) {
        CAPTURE(repeat);
        REQUIRE(run() == baseline);
    }
}

TEST_CASE(
    "endpoint identities bind dynamic slots but permit distinct static shapes",
    "[physics][persistent-contact][identity][endpoint-binding]")
{
    using namespace shark;

    std::array states{body_at({}, {0.0F, -1.0F, 0.0F})};
    const std::array masses{mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
    };
    const std::array descriptors{
        descriptor(identity(10U, 20U, 1U, 1U)),
        descriptor(identity(11U, 20U, 1U, 2U)),
    };
    physics::ContactManifoldCache cache{};
    const auto result =
        physics::solve_persistent_contact_constraints(
            states,
            masses,
            constraints,
            descriptors,
            1U,
            cache);
    REQUIRE(result);
    REQUIRE(cache.manifold_count == 2U);
}

TEST_CASE(
    "warm starting materially improves a one-sweep support chain",
    "[physics][persistent-contact][warm-start][stack][convergence]")
{
    using namespace shark;

    const std::array initial_states{
        body_at({}, {0.0F, -1.0F, 0.0F}),
        body_at({0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
        body_at({0.0F, 2.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
    };
    const std::array masses{mass(), mass(), mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
        constraint(0U, 1U, {0.0F, 1.0F, 0.0F}),
        constraint(1U, 2U, {0.0F, 1.0F, 0.0F}),
    };
    auto one_sweep = physics::ContactSolverSettings{};
    one_sweep.velocity_iterations = 1U;

    auto cold_states = initial_states;
    const auto cold = physics::solve_contact_constraints(
        cold_states,
        masses,
        constraints,
        one_sweep);
    REQUIRE(cold);
    const auto cold_residual =
        std::max({
            std::abs(cold_states[0].linear_velocity.y),
            std::abs(cold_states[1].linear_velocity.y),
            std::abs(cold_states[2].linear_velocity.y),
        });

    const std::array warm_starts{
        physics::ContactConstraintWarmStart{
            .points = {{
                physics::ContactPointWarmStart{
                    .normal_impulse_magnitude = 3.0F,
                },
            }},
            .point_count = 1U,
        },
        physics::ContactConstraintWarmStart{
            .points = {{
                physics::ContactPointWarmStart{
                    .normal_impulse_magnitude = 2.0F,
                },
            }},
            .point_count = 1U,
        },
        physics::ContactConstraintWarmStart{
            .points = {{
                physics::ContactPointWarmStart{
                    .normal_impulse_magnitude = 1.0F,
                },
            }},
            .point_count = 1U,
        },
    };
    auto warm_states = initial_states;
    const auto warm = physics::solve_contact_constraints(
        warm_states,
        masses,
        constraints,
        warm_starts,
        one_sweep);
    REQUIRE(warm);
    require_float3(warm_states[0].linear_velocity, {});
    require_float3(warm_states[1].linear_velocity, {});
    require_float3(warm_states[2].linear_velocity, {});
    REQUIRE(cold_residual > 0.5F);

    auto reference_states = initial_states;
    auto reference_settings = physics::ContactSolverSettings{};
    reference_settings.velocity_iterations = 32U;
    const auto reference = physics::solve_contact_constraints(
        reference_states,
        masses,
        constraints,
        reference_settings);
    REQUIRE(reference);
    const auto cold_reference_error =
        std::abs(
            cold_states[0].linear_velocity.y -
            reference_states[0].linear_velocity.y) +
        std::abs(
            cold_states[1].linear_velocity.y -
            reference_states[1].linear_velocity.y) +
        std::abs(
            cold_states[2].linear_velocity.y -
            reference_states[2].linear_velocity.y);
    const auto warm_reference_error =
        std::abs(
            warm_states[0].linear_velocity.y -
            reference_states[0].linear_velocity.y) +
        std::abs(
            warm_states[1].linear_velocity.y -
            reference_states[1].linear_velocity.y) +
        std::abs(
            warm_states[2].linear_velocity.y -
            reference_states[2].linear_velocity.y);
    REQUIRE(warm_reference_error < cold_reference_error);
}

TEST_CASE(
    "malformed direct warm starts leave body state unchanged",
    "[physics][persistent-contact][warm-start][validation]"
    "[immutability]")
{
    using namespace shark;

    const std::array initial_states{
        body_at({}, {0.0F, -1.0F, 0.0F}),
    };
    const std::array masses{mass()};
    const std::array constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
    };

    SECTION("point count mismatch")
    {
        auto states = initial_states;
        const auto before = states;
        const std::array warm_starts{
            physics::ContactConstraintWarmStart{},
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints,
            warm_starts);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() == core::ErrorCode::invalid_argument);
        REQUIRE(states == before);
    }

    SECTION("negative seed")
    {
        auto states = initial_states;
        const auto before = states;
        const std::array warm_starts{
            physics::ContactConstraintWarmStart{
                .points = {{
                    physics::ContactPointWarmStart{
                        .normal_impulse_magnitude = -1.0F,
                    },
                }},
                .point_count = 1U,
            },
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints,
            warm_starts);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() == core::ErrorCode::invalid_argument);
        REQUIRE(states == before);
    }
}

TEST_CASE(
    "persistent cache validation and numerical failures roll back",
    "[physics][persistent-contact][validation][overflow][immutability]")
{
    using namespace shark;

    const std::array baseline_states{
        body_at({}, {0.0F, -1.0F, 0.0F}),
    };
    const std::array baseline_masses{mass()};
    const std::array baseline_constraints{
        constraint(
            physics::static_contact_body_index,
            0U,
            {0.0F, 1.0F, 0.0F}),
    };
    const std::array baseline_descriptors{
        descriptor(identity(10U, 20U)),
    };

    SECTION("duplicate identity")
    {
        const std::array states{
            baseline_states[0],
            body_at({0.0F, 2.0F, 0.0F}),
        };
        const std::array masses{mass(), mass()};
        const std::array constraints{
            baseline_constraints[0],
            constraint(
                physics::static_contact_body_index,
                1U,
                {0.0F, 1.0F, 0.0F}),
        };
        const std::array descriptors{
            baseline_descriptors[0],
            baseline_descriptors[0],
        };
        require_persistent_failure_is_transactional(
            states,
            masses,
            constraints,
            descriptors,
            1U,
            {});
    }

    SECTION("one dynamic slot cannot claim two stable endpoint IDs")
    {
        const std::array constraints{
            baseline_constraints[0],
            baseline_constraints[0],
        };
        const std::array descriptors{
            descriptor(identity(10U, 20U)),
            descriptor(identity(11U, 21U)),
        };
        require_persistent_failure_is_transactional(
            baseline_states,
            baseline_masses,
            constraints,
            descriptors,
            1U,
            {});
    }

    SECTION("nonmonotonic tick")
    {
        auto cache = one_point_cache(
            identity(10U, 20U),
            {0.0F, 1.0F, 0.0F},
            {},
            {},
            1.0F,
            {},
            5U,
            true,
            false);
        require_persistent_failure_is_transactional(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            baseline_descriptors,
            5U,
            cache);
    }

    SECTION("malformed late cache entry")
    {
        auto cache = one_point_cache(
            identity(10U, 20U),
            {0.0F, 1.0F, 0.0F},
            {},
            {},
            1.0F,
            {},
            5U,
            true,
            false);
        cache.manifold_count = 2U;
        cache.manifolds[1] = cache.manifolds[0];
        cache.manifolds[1].identity = identity(30U, 40U);
        cache.manifolds[1].points[0].first_anchor.x =
            std::numeric_limits<float>::quiet_NaN();
        require_persistent_failure_is_transactional(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            baseline_descriptors,
            6U,
            cache);
    }

    SECTION("invalid settings")
    {
        auto settings = physics::ContactPersistenceSettings{};
        settings.minimum_normal_alignment = 1.1F;
        require_persistent_failure_is_transactional(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            baseline_descriptors,
            1U,
            {},
            core::ErrorCode::invalid_argument,
            settings);
    }

    SECTION("cache capacity")
    {
        physics::ContactManifoldCache cache{
            .manifold_count =
                physics::contact_manifold_cache_capacity,
            .last_committed_tick = 1U,
            .has_committed_tick = true,
        };
        for (std::size_t index = 0U;
             index < physics::contact_manifold_cache_capacity;
             ++index) {
            cache.manifolds[index] =
                physics::CachedContactManifold{
                    .identity = identity(
                        static_cast<std::uint64_t>(100U + index * 2U),
                        static_cast<std::uint64_t>(101U + index * 2U)),
                    .normal = {0.0F, 1.0F, 0.0F},
                    .points = {{physics::CachedContactPoint{}}},
                    .point_count = 1U,
                    .last_seen_tick = 1U,
                };
        }
        require_persistent_failure_is_transactional(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            baseline_descriptors,
            2U,
            cache);
    }

    SECTION("late solver overflow")
    {
        const auto maximum = std::numeric_limits<float>::max();
        const std::array states{
            body_at({}, {maximum * 0.75F, 0.0F, 0.0F}),
            body_at({1.0F, 0.0F, 0.0F}),
        };
        const std::array masses{mass(0.000001F), mass()};
        const std::array constraints{
            constraint(
                0U,
                1U,
                {1.0F, 0.0F, 0.0F},
                {},
                physics::ContactMaterial{
                    .restitution = 1.0F,
                }),
        };
        const std::array descriptors{
            descriptor(identity(10U, 20U)),
        };
        auto cache = one_point_cache(
            identity(30U, 40U),
            {0.0F, 1.0F, 0.0F},
            {},
            {},
            0.0F,
            {},
            1U,
            true,
            false);
        require_persistent_failure_is_transactional(
            states,
            masses,
            constraints,
            descriptors,
            2U,
            cache,
            core::ErrorCode::unavailable);
    }
}
