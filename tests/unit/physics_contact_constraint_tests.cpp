#include <shark/physics/contact_constraint.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    const shark::math::Float3 local_inverse_inertia = {})
{
    return shark::physics::ContactBodyMassProperties{
        .inverse_mass = inverse_mass,
        .local_inverse_inertia = local_inverse_inertia,
    };
}

[[nodiscard]] shark::physics::ContactMaterial material(
    const float restitution = 0.0F,
    const float static_friction = 0.0F,
    const float dynamic_friction = 0.0F)
{
    return shark::physics::ContactMaterial{
        .restitution = restitution,
        .static_friction = static_friction,
        .dynamic_friction = dynamic_friction,
    };
}

[[nodiscard]] shark::physics::ContactConstraint one_point_constraint(
    const std::size_t first_body_index,
    const std::size_t second_body_index,
    const shark::math::Float3 normal,
    const shark::math::Float3 position,
    const float penetration_depth = 0.0F,
    const shark::physics::ContactMaterial contact_material = {})
{
    return shark::physics::ContactConstraint{
        .first_body_index = first_body_index,
        .second_body_index = second_body_index,
        .normal = normal,
        .material = contact_material,
        .points = {
            shark::physics::ContactConstraintPoint{
                .position = position,
                .penetration_depth = penetration_depth,
            },
        },
        .point_count = 1U,
    };
}

template<std::size_t StateCount,
         std::size_t MassCount,
         std::size_t ConstraintCount>
void require_invalid_transaction(
    std::array<shark::physics::RigidBodyState, StateCount> states,
    const std::array<
        shark::physics::ContactBodyMassProperties,
        MassCount>& masses,
    const std::array<
        shark::physics::ContactConstraint,
        ConstraintCount>& constraints,
    const shark::physics::ContactSolverSettings settings = {})
{
    const auto before = states;
    const auto result = shark::physics::solve_contact_constraints(
        states,
        masses,
        constraints,
        settings);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
    REQUIRE(states == before);
}

} // namespace

TEST_CASE(
    "contact solver capacities and empty input are explicit",
    "[physics][contact-constraint][capacity][empty]")
{
    using namespace shark;

    STATIC_REQUIRE(physics::contact_solver_body_capacity == 4U);
    STATIC_REQUIRE(physics::contact_constraint_capacity == 10U);
    STATIC_REQUIRE(physics::contact_points_per_constraint == 4U);
    STATIC_REQUIRE(
        physics::max_contact_solver_velocity_iterations == 32U);
    STATIC_REQUIRE(
        physics::static_contact_body_index ==
        std::numeric_limits<std::size_t>::max());

    std::array<physics::RigidBodyState, 0> states{};
    constexpr std::array<
        physics::ContactBodyMassProperties,
        0> masses{};
    constexpr std::array<physics::ContactConstraint, 0> constraints{};
    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints);
    REQUIRE(result);
    REQUIRE(result.value().constraint_count == 0U);
    REQUIRE(
        result.value().completed_velocity_iterations ==
        physics::ContactSolverSettings{}.velocity_iterations);
    REQUIRE(states.empty());
}

TEST_CASE(
    "contact solver applies analytic unequal-mass restitution",
    "[physics][contact-constraint][normal][restitution][momentum]")
{
    using namespace shark;

    std::array states{
        body_at({-1.0F, 0.0F, 0.0F}, {3.0F, 2.0F, -4.0F}),
        body_at({1.0F, 0.0F, 0.0F}, {-1.0F, -5.0F, 6.0F}),
    };
    constexpr std::array masses{
        physics::ContactBodyMassProperties{
            .inverse_mass = 0.5F,
        },
        physics::ContactBodyMassProperties{
            .inverse_mass = 1.0F,
        },
    };
    const std::array constraints{
        one_point_constraint(
            0U,
            1U,
            {1.0F, 0.0F, 0.0F},
            {},
            0.0F,
            material(0.5F)),
    };

    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints);
    REQUIRE(result);
    REQUIRE(result.value().constraint_count == 1U);
    REQUIRE(result.value().constraints[0].point_count == 1U);
    const auto& impulse = result.value().constraints[0].points[0];
    REQUIRE(
        impulse.relative_normal_velocity_before_resolution ==
        Catch::Approx(-4.0F));
    REQUIRE(
        impulse.normal_impulse_magnitude ==
        Catch::Approx(4.0F));
    require_float3(impulse.tangent_impulse, {});
    require_float3(
        states[0].linear_velocity,
        {1.0F, 2.0F, -4.0F});
    require_float3(
        states[1].linear_velocity,
        {3.0F, -5.0F, 6.0F});
    REQUIRE(
        2.0F * states[0].linear_velocity.x +
            states[1].linear_velocity.x ==
        Catch::Approx(5.0F));
    REQUIRE(
        states[1].linear_velocity.x -
            states[0].linear_velocity.x ==
        Catch::Approx(2.0F));
    require_float3(states[0].angular_velocity, {});
    require_float3(states[1].angular_velocity, {});
}

TEST_CASE(
    "contact solver never pulls separating bodies or reapplies restitution",
    "[physics][contact-constraint][separating][restitution]"
    "[iterations]")
{
    using namespace shark;

    SECTION("separating contact")
    {
        std::array states{
            body_at({-1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
            body_at({1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        };
        const auto before = states;
        const std::array masses{mass(), mass()};
        const std::array constraints{
            one_point_constraint(
                0U,
                1U,
                {1.0F, 0.0F, 0.0F},
                {},
                0.0F,
                material(1.0F)),
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints);
        REQUIRE(result);
        REQUIRE(states == before);
        REQUIRE(
            result.value()
                    .constraints[0]
                    .points[0]
                    .relative_normal_velocity_before_resolution ==
                Catch::Approx(2.0F));
        REQUIRE(
            result.value()
                    .constraints[0]
                    .points[0]
                    .normal_impulse_magnitude ==
                0.0F);
    }

    SECTION("restitution target is captured once")
    {
        const std::array initial_states{
            body_at({-1.0F, 0.0F, 0.0F}, {3.0F, 0.0F, 0.0F}),
            body_at({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
        };
        const std::array masses{mass(0.5F), mass(1.0F)};
        const std::array constraints{
            one_point_constraint(
                0U,
                1U,
                {1.0F, 0.0F, 0.0F},
                {},
                0.0F,
                material(0.5F)),
        };
        auto one_iteration_states = initial_states;
        auto eight_iteration_states = initial_states;
        auto one_iteration_settings =
            physics::ContactSolverSettings{};
        one_iteration_settings.velocity_iterations = 1U;
        const auto one_iteration =
            physics::solve_contact_constraints(
                one_iteration_states,
                masses,
                constraints,
                one_iteration_settings);
        const auto eight_iterations =
            physics::solve_contact_constraints(
                eight_iteration_states,
                masses,
                constraints);
        REQUIRE(one_iteration);
        REQUIRE(eight_iterations);
        REQUIRE(one_iteration_states == eight_iteration_states);
        REQUIRE(
            one_iteration.value().constraints[0] ==
            eight_iterations.value().constraints[0]);
        REQUIRE(
            one_iteration.value().completed_velocity_iterations ==
            1U);
        REQUIRE(
            eight_iterations.value().completed_velocity_iterations ==
            8U);
    }
}

TEST_CASE(
    "off-center contact impulse produces the analytic angular response",
    "[physics][contact-constraint][angular][effective-mass]")
{
    using namespace shark;

    std::array states{
        body_at({0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
    };
    const std::array masses{
        mass(1.0F, {1.0F, 1.0F, 1.0F}),
    };
    const std::array constraints{
        one_point_constraint(
            physics::static_contact_body_index,
            0U,
            {1.0F, 0.0F, 0.0F},
            {}),
    };
    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints);
    REQUIRE(result);
    const auto& impulse = result.value().constraints[0].points[0];
    REQUIRE(
        impulse.relative_normal_velocity_before_resolution ==
        Catch::Approx(-1.0F));
    REQUIRE(
        impulse.normal_impulse_magnitude ==
        Catch::Approx(0.5F));
    require_float3(
        states[0].linear_velocity,
        {-0.5F, 0.0F, 0.0F});
    require_float3(
        states[0].angular_velocity,
        {0.0F, 0.0F, 0.5F});
}

TEST_CASE(
    "symmetric two-point contact cancels net angular response",
    "[physics][contact-constraint][manifold][angular]")
{
    using namespace shark;

    std::array states{
        body_at({0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
    };
    const std::array masses{
        mass(1.0F, {1.0F, 1.0F, 1.0F}),
    };
    const std::array constraints{
        physics::ContactConstraint{
            .first_body_index =
                physics::static_contact_body_index,
            .second_body_index = 0U,
            .normal = {0.0F, 1.0F, 0.0F},
            .points = {
                physics::ContactConstraintPoint{
                    .position = {-1.0F, 0.0F, 0.0F},
                },
                physics::ContactConstraintPoint{
                    .position = {1.0F, 0.0F, 0.0F},
                },
            },
            .point_count = 2U,
        },
    };
    auto settings = physics::ContactSolverSettings{};
    settings.velocity_iterations = 1U;
    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints,
        settings);
    REQUIRE(result);
    REQUIRE(result.value().constraints[0].point_count == 2U);
    REQUIRE(
        result.value().constraints[0].points[0]
                .normal_impulse_magnitude ==
            Catch::Approx(0.5F));
    REQUIRE(
        result.value().constraints[0].points[1]
                .normal_impulse_magnitude ==
            Catch::Approx(0.5F));
    require_float3(states[0].linear_velocity, {});
    require_float3(states[0].angular_velocity, {});
}

TEST_CASE(
    "contact friction clamps dynamic sliding and permits static sticking",
    "[physics][contact-constraint][friction][coulomb]")
{
    using namespace shark;

    const auto run = [](const physics::ContactMaterial contact_material) {
        std::array states{
            body_at({}, {4.0F, -2.0F, 0.0F}),
        };
        const std::array masses{mass()};
        const std::array constraints{
            one_point_constraint(
                physics::static_contact_body_index,
                0U,
                {0.0F, 1.0F, 0.0F},
                {},
                0.0F,
                contact_material),
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints);
        REQUIRE(result);
        return std::pair{states[0], result.value()};
    };

    SECTION("dynamic friction clamp")
    {
        const auto [state, step] = run(material(0.0F, 1.0F, 0.5F));
        require_float3(state.linear_velocity, {3.0F, 0.0F, 0.0F});
        const auto& impulse = step.constraints[0].points[0];
        REQUIRE(impulse.normal_impulse_magnitude ==
            Catch::Approx(2.0F));
        require_float3(
            impulse.tangent_impulse,
            {-1.0F, 0.0F, 0.0F});
    }

    SECTION("static friction stick")
    {
        const auto [state, step] = run(material(0.0F, 3.0F, 0.5F));
        require_float3(state.linear_velocity, {});
        const auto& impulse = step.constraints[0].points[0];
        REQUIRE(impulse.normal_impulse_magnitude ==
            Catch::Approx(2.0F));
        require_float3(
            impulse.tangent_impulse,
            {-4.0F, 0.0F, 0.0F});
    }
}

TEST_CASE(
    "static contact endpoint stays immutable while restitution acts",
    "[physics][contact-constraint][static][restitution]")
{
    using namespace shark;

    const auto run = [](const bool static_is_first) {
        std::array states{
            body_at({0.0F, 1.0F, 0.0F}, {0.0F, -3.0F, 0.0F}),
        };
        const auto position_before = states[0].position;
        const std::array masses{mass(0.5F)};
        const std::array constraints{
            one_point_constraint(
                static_is_first
                    ? physics::static_contact_body_index
                    : 0U,
                static_is_first
                    ? 0U
                    : physics::static_contact_body_index,
                static_is_first
                    ? math::Float3{0.0F, 1.0F, 0.0F}
                    : math::Float3{0.0F, -1.0F, 0.0F},
                {0.0F, 1.0F, 0.0F},
                0.0F,
                material(0.25F)),
        };
        const auto constraints_before = constraints;
        const auto masses_before = masses;
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints);
        REQUIRE(result);
        require_float3(
            states[0].linear_velocity,
            {0.0F, 0.75F, 0.0F});
        REQUIRE(states[0].position == position_before);
        REQUIRE(
            result.value().constraints[0].points[0]
                    .normal_impulse_magnitude ==
                Catch::Approx(7.5F));
        REQUIRE(constraints == constraints_before);
        REQUIRE(masses == masses_before);
    };

    SECTION("static first endpoint")
    {
        run(true);
    }

    SECTION("static second endpoint")
    {
        run(false);
    }
}

TEST_CASE(
    "Coulomb friction holds or slides on an analytic slope",
    "[physics][contact-constraint][friction][slope]")
{
    using namespace shark;

    constexpr auto inverse_sqrt_five = 0.4472135954999579F;
    constexpr math::Float3 slope_normal{
        -inverse_sqrt_five,
        2.0F * inverse_sqrt_five,
        0.0F,
    };
    const auto run = [](const float friction) {
        std::array states{
            body_at({}, {0.0F, -1.0F, 0.0F}),
        };
        const std::array masses{
            mass(1.0F, {}),
        };
        const std::array constraints{
            one_point_constraint(
                physics::static_contact_body_index,
                0U,
                slope_normal,
                {},
                0.0F,
                material(0.0F, friction, friction)),
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints);
        REQUIRE(result);
        return std::pair{states[0], result.value()};
    };

    SECTION("friction above tangent of slope holds")
    {
        const auto [state, step] = run(0.6F);
        require_float3(state.linear_velocity, {}, 0.000002F);
        REQUIRE(
            step.constraints[0].points[0]
                    .normal_impulse_magnitude ==
                Catch::Approx(2.0F * inverse_sqrt_five)
                    .margin(0.000002F));
        require_float3(
            step.constraints[0].points[0].tangent_impulse,
            {0.4F, 0.2F, 0.0F},
            0.000002F);
    }

    SECTION("insufficient friction slides")
    {
        const auto [state, step] = run(0.25F);
        require_float3(
            state.linear_velocity,
            {-0.2F, -0.1F, 0.0F},
            0.000002F);
        require_float3(
            step.constraints[0].points[0].tangent_impulse,
            {0.2F, 0.1F, 0.0F},
            0.000002F);
    }
}

TEST_CASE(
    "position correction is deepest slop-aware bounded and mass-weighted",
    "[physics][contact-constraint][penetration][correction]")
{
    using namespace shark;

    const std::array masses{mass(0.5F), mass(1.0F)};
    auto settings = physics::ContactSolverSettings{};
    settings.velocity_iterations = 1U;
    settings.penetration_slop = 0.001F;
    settings.penetration_correction_fraction = 0.8F;
    settings.maximum_position_correction = 0.2F;

    const auto run = [&](const float shallow_depth,
                         const float deepest_depth) {
        std::array states{
            body_at({0.0F, 0.0F, 0.0F}),
            body_at({1.8F, 0.0F, 0.0F}),
        };
        const std::array constraints{
            physics::ContactConstraint{
                .first_body_index = 0U,
                .second_body_index = 1U,
                .normal = {1.0F, 0.0F, 0.0F},
                .points = {
                    physics::ContactConstraintPoint{
                        .position = {0.9F, 0.0F, 0.0F},
                        .penetration_depth = shallow_depth,
                    },
                    physics::ContactConstraintPoint{
                        .position = {0.9F, 0.0F, 0.0F},
                        .penetration_depth = deepest_depth,
                    },
                },
                .point_count = 2U,
            },
        };
        const auto result = physics::solve_contact_constraints(
            states,
            masses,
            constraints,
            settings);
        REQUIRE(result);
        return states;
    };

    SECTION("deepest point and inverse-mass shares")
    {
        const auto states = run(0.051F, 0.101F);
        require_float3(
            states[0].position,
            {-0.026666667F, 0.0F, 0.0F});
        require_float3(
            states[1].position,
            {1.85333335F, 0.0F, 0.0F});
    }

    SECTION("maximum correction cap")
    {
        const auto states = run(0.5F, 1.001F);
        require_float3(
            states[0].position,
            {-0.06666667F, 0.0F, 0.0F});
        require_float3(
            states[1].position,
            {1.93333328F, 0.0F, 0.0F});
    }

    SECTION("penetration within slop")
    {
        const auto states = run(0.0005F, 0.001F);
        require_float3(states[0].position, {});
        require_float3(states[1].position, {1.8F, 0.0F, 0.0F});
    }
}

TEST_CASE(
    "contact constraints retain supplied order for every iteration",
    "[physics][contact-constraint][order][iterations][determinism]")
{
    using namespace shark;

    const std::array initial_states{
        body_at({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        body_at({1.0F, 0.0F, 0.0F}),
        body_at({2.0F, 0.0F, 0.0F}),
    };
    const std::array masses{mass(), mass(), mass()};
    const std::array constraints{
        one_point_constraint(
            0U,
            1U,
            {1.0F, 0.0F, 0.0F},
            {0.5F, 0.0F, 0.0F}),
        one_point_constraint(
            1U,
            2U,
            {1.0F, 0.0F, 0.0F},
            {1.5F, 0.0F, 0.0F}),
    };

    auto one_sweep_states = initial_states;
    auto one_sweep_settings = physics::ContactSolverSettings{};
    one_sweep_settings.velocity_iterations = 1U;
    const auto one_sweep = physics::solve_contact_constraints(
        one_sweep_states,
        masses,
        constraints,
        one_sweep_settings);
    REQUIRE(one_sweep);
    require_float3(
        one_sweep_states[0].linear_velocity,
        {0.5F, 0.0F, 0.0F});
    require_float3(
        one_sweep_states[1].linear_velocity,
        {0.25F, 0.0F, 0.0F});
    require_float3(
        one_sweep_states[2].linear_velocity,
        {0.25F, 0.0F, 0.0F});
    REQUIRE(one_sweep.value().completed_velocity_iterations == 1U);

    auto two_sweep_states = initial_states;
    auto two_sweep_settings = physics::ContactSolverSettings{};
    two_sweep_settings.velocity_iterations = 2U;
    const auto two_sweeps = physics::solve_contact_constraints(
        two_sweep_states,
        masses,
        constraints,
        two_sweep_settings);
    REQUIRE(two_sweeps);
    require_float3(
        two_sweep_states[0].linear_velocity,
        {0.375F, 0.0F, 0.0F});
    require_float3(
        two_sweep_states[1].linear_velocity,
        {0.3125F, 0.0F, 0.0F});
    require_float3(
        two_sweep_states[2].linear_velocity,
        {0.3125F, 0.0F, 0.0F});
    REQUIRE(two_sweeps.value().completed_velocity_iterations == 2U);
    REQUIRE(
        two_sweeps.value().constraints[0].points[0]
                .normal_impulse_magnitude ==
            Catch::Approx(0.625F));
    REQUIRE(
        two_sweeps.value().constraints[1].points[0]
                .normal_impulse_magnitude ==
            Catch::Approx(0.3125F));

    for (std::size_t repeat = 0U; repeat < 64U; ++repeat) {
        CAPTURE(repeat);
        auto repeated_states = initial_states;
        const auto repeated = physics::solve_contact_constraints(
            repeated_states,
            masses,
            constraints,
            two_sweep_settings);
        REQUIRE(repeated);
        REQUIRE(repeated_states == two_sweep_states);
        REQUIRE(repeated.value() == two_sweeps.value());
    }
}

TEST_CASE(
    "contact solver rejects malformed batches before mutation",
    "[physics][contact-constraint][validation][immutability]")
{
    using namespace shark;

    const std::array baseline_states{
        body_at({-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        body_at({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
    };
    const std::array baseline_masses{mass(), mass()};
    const std::array baseline_constraints{
        one_point_constraint(
            0U,
            1U,
            {1.0F, 0.0F, 0.0F},
            {}),
    };

    SECTION("mismatched body data")
    {
        const std::array short_masses{mass()};
        require_invalid_transaction(
            baseline_states,
            short_masses,
            baseline_constraints);
    }

    SECTION("nonfinite body or mass")
    {
        auto states = baseline_states;
        states[0].position.x =
            std::numeric_limits<float>::infinity();
        require_invalid_transaction(
            states,
            baseline_masses,
            baseline_constraints);

        auto masses = baseline_masses;
        masses[1].local_inverse_inertia.y =
            std::numeric_limits<float>::quiet_NaN();
        require_invalid_transaction(
            baseline_states,
            masses,
            baseline_constraints);
    }

    SECTION("invalid endpoints and normal")
    {
        auto constraints = baseline_constraints;
        constraints[0].second_body_index = 2U;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].second_body_index = 0U;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].first_body_index =
            physics::static_contact_body_index;
        constraints[0].second_body_index =
            physics::static_contact_body_index;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].normal = {2.0F, 0.0F, 0.0F};
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);
    }

    SECTION("invalid material point and settings")
    {
        auto constraints = baseline_constraints;
        constraints[0].material = material(1.1F);
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].material = material(0.0F, 0.25F, 0.5F);
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].points[0].penetration_depth = -0.1F;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        constraints = baseline_constraints;
        constraints[0].points[0].position.z =
            std::numeric_limits<float>::quiet_NaN();
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);

        auto settings = physics::ContactSolverSettings{};
        settings.velocity_iterations = 0U;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            settings);

        settings = physics::ContactSolverSettings{};
        settings.penetration_correction_fraction = 1.1F;
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            baseline_constraints,
            settings);
    }

    SECTION("late malformed constraint still rolls back the batch")
    {
        std::array constraints{
            baseline_constraints[0],
            baseline_constraints[0],
        };
        constraints[1].points[0].position.x =
            std::numeric_limits<float>::infinity();
        require_invalid_transaction(
            baseline_states,
            baseline_masses,
            constraints);
    }
}

TEST_CASE(
    "contact solver rolls back finite input whose result overflows float",
    "[physics][contact-constraint][overflow][immutability]")
{
    using namespace shark;

    const auto maximum = std::numeric_limits<float>::max();
    std::array states{
        body_at(
            {-1.0F, 0.0F, 0.0F},
            {maximum * 0.75F, 0.0F, 0.0F}),
        body_at({1.0F, 0.0F, 0.0F}),
    };
    const auto before = states;
    const std::array masses{mass(0.000001F), mass(1.0F)};
    const std::array constraints{
        one_point_constraint(
            0U,
            1U,
            {1.0F, 0.0F, 0.0F},
            {},
            0.0F,
            material(1.0F)),
    };
    auto settings = physics::ContactSolverSettings{};
    settings.restitution_velocity_threshold = 0.0F;
    const auto result = physics::solve_contact_constraints(
        states,
        masses,
        constraints,
        settings);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() == core::ErrorCode::unavailable);
    REQUIRE(states == before);
}
