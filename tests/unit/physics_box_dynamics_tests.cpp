#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/box_contact.hpp>
#include <shark/physics/persistent_contact.hpp>
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
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr float comparison_margin = 0.00001F;
constexpr std::size_t stack_body_count = 3U;
constexpr float stack_x = 1.0F;
constexpr float stack_z = 3.0F;

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

[[nodiscard]] float vector_length(
    const shark::math::Float3 value) noexcept
{
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
}

[[nodiscard]] shark::math::Quaternion axis_angle(
    const shark::math::Float3 unit_axis,
    const float angle) noexcept
{
    const auto sine = std::sin(angle * 0.5F);
    return {
        unit_axis.x * sine,
        unit_axis.y * sine,
        unit_axis.z * sine,
        std::cos(angle * 0.5F),
    };
}

[[nodiscard]] shark::math::Float3 world_angular_momentum(
    const shark::physics::RigidBodyState& state,
    const shark::physics::RigidBodyMassProperties& properties) noexcept
{
    const shark::math::Quaternion inverse{
        -state.orientation.x,
        -state.orientation.y,
        -state.orientation.z,
        state.orientation.w,
    };
    const auto local_velocity = shark::math::rotate(
        inverse,
        state.angular_velocity);
    return shark::math::rotate(
        state.orientation,
        {
            local_velocity.x *
                properties.local_moment_of_inertia.x,
            local_velocity.y *
                properties.local_moment_of_inertia.y,
            local_velocity.z *
                properties.local_moment_of_inertia.z,
        });
}

[[nodiscard]] shark::terrain::HeightTileSurface make_stack_surface()
{
    auto result = shark::terrain::HeightTileSurface::create(
        shark::terrain::HeightTile{
            .sample_columns = 3U,
            .sample_rows = 3U,
            .sample_spacing = 4.0F,
            .origin = {},
            .height_offsets = std::vector<float>(9U, 0.0F),
        });
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::physics::ContactManifoldIdentity terrain_identity(
    const std::size_t body_index) noexcept
{
    return {
        .first_endpoint = 1U,
        .second_endpoint =
            static_cast<std::uint64_t>(100U + body_index),
        .first_shape = 1U,
        .second_shape = 1U,
    };
}

[[nodiscard]] shark::physics::ContactManifoldIdentity pair_identity(
    const std::size_t first,
    const std::size_t second) noexcept
{
    return {
        .first_endpoint =
            static_cast<std::uint64_t>(100U + first),
        .second_endpoint =
            static_cast<std::uint64_t>(100U + second),
        .first_shape = 1U,
        .second_shape = 1U,
    };
}

struct StackMetrics final {
    float maximum_horizontal_drift{};
    float maximum_height_error{};
    float maximum_penetration{};
    float maximum_post_solve_penetration{};
    float maximum_linear_speed{};
    float maximum_angular_speed{};
    float maximum_tilt_degrees{};
    std::uint32_t final_window_ticks{};
    std::uint32_t final_window_three_manifold_ticks{};
    std::uint32_t final_window_twelve_point_ticks{};
    std::uint32_t final_window_full_warm_start_ticks{};
    std::size_t last_warm_started_point_count{};

    [[nodiscard]] friend bool operator==(
        const StackMetrics&,
        const StackMetrics&) noexcept = default;
};

struct StackRun final {
    std::array<
        shark::physics::RigidBodyState,
        stack_body_count> states{};
    shark::physics::ContactManifoldCache cache{};
    StackMetrics metrics{};
    std::uint64_t step_count{};

    [[nodiscard]] friend bool operator==(
        const StackRun&,
        const StackRun&) noexcept = default;
};

[[nodiscard]] StackRun run_stack_schedule(
    const std::uint32_t render_rate_hz,
    const std::uint32_t solver_iterations = 8U)
{
    using namespace shark;

    const auto surface = make_stack_surface();
    constexpr physics::BoxCollider collider{
        .local_half_extents = {0.5F, 0.5F, 0.5F},
    };
    const auto mass_result =
        physics::make_solid_box_mass_properties(1.0F, collider);
    REQUIRE(mass_result);
    const auto box_mass = mass_result.value();
    const std::array mass_properties{
        physics::ContactBodyMassProperties{
            .inverse_mass = box_mass.body.inverse_mass,
            .local_inverse_inertia =
                box_mass.body.local_inverse_moment_of_inertia,
        },
        physics::ContactBodyMassProperties{
            .inverse_mass = box_mass.body.inverse_mass,
            .local_inverse_inertia =
                box_mass.body.local_inverse_moment_of_inertia,
        },
        physics::ContactBodyMassProperties{
            .inverse_mass = box_mass.body.inverse_mass,
            .local_inverse_inertia =
                box_mass.body.local_inverse_moment_of_inertia,
        },
    };
    std::array<physics::RigidBodyState, stack_body_count> states{
        physics::RigidBodyState{
            .position = {stack_x, 0.5F, stack_z},
        },
        physics::RigidBodyState{
            .position = {stack_x, 1.5F, stack_z},
        },
        physics::RigidBodyState{
            .position = {stack_x, 2.5F, stack_z},
        },
    };
    physics::ContactManifoldCache cache{};
    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    auto previous_timestamp = std::chrono::nanoseconds{0};
    constexpr std::uint32_t duration_seconds = 10U;
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;
    auto solver_settings = physics::ContactSolverSettings{};
    solver_settings.velocity_iterations = solver_iterations;
    solver_settings.penetration_slop = 0.0005F;
    solver_settings.penetration_correction_fraction = 1.0F;
    StackMetrics metrics{};

    for (std::uint64_t frame = 1U; frame <= frame_count; ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;

        for (std::uint32_t step = 0U;
             step < frame_result.value().step_count;
             ++step) {
            for (auto& state : states) {
                REQUIRE(physics::advance_ballistic_body(
                    state,
                    physics::standard_gravity,
                    clock.fixed_delta_seconds()));
                REQUIRE(physics::advance_rigid_body_angular_motion(
                    state,
                    box_mass.body,
                    {},
                    clock.fixed_delta_seconds()));
            }

            std::array<
                physics::ContactConstraint,
                physics::contact_constraint_capacity>
                constraints{};
            std::array<
                physics::ContactPersistenceDescriptor,
                physics::contact_constraint_capacity>
                descriptors{};
            std::size_t constraint_count = 0U;

            for (std::size_t body_index = 0U;
                 body_index < states.size();
                 ++body_index) {
                const auto query = physics::query_box_terrain_contact(
                    states[body_index],
                    collider,
                    surface);
                REQUIRE(query);
                if (!query.value().has_value()) {
                    continue;
                }
                const auto& manifold = *query.value();
                auto& contact = constraints[constraint_count];
                auto& persistence = descriptors[constraint_count];
                contact.first_body_index =
                    physics::static_contact_body_index;
                contact.second_body_index = body_index;
                contact.normal = manifold.normal;
                contact.material = {
                    .restitution = 0.0F,
                    .static_friction = 0.8F,
                    .dynamic_friction = 0.6F,
                };
                contact.point_count = manifold.point_count;
                persistence.identity = terrain_identity(body_index);
                persistence.point_count = manifold.point_count;
                for (std::size_t point_index = 0U;
                     point_index < manifold.point_count;
                     ++point_index) {
                    const auto& point = manifold.points[point_index];
                    contact.points[point_index] = {
                        .position = point.position,
                        .penetration_depth = point.penetration_depth,
                    };
                    persistence.points[point_index] = {
                        .point_on_first = point.surface.position,
                        .point_on_second = point.box_point,
                    };
                }
                ++constraint_count;
            }

            for (std::size_t first = 0U;
                 first < states.size();
                 ++first) {
                for (std::size_t second = first + 1U;
                     second < states.size();
                     ++second) {
                    const auto query = physics::query_box_box_contact(
                        states[first],
                        collider,
                        states[second],
                        collider);
                    REQUIRE(query);
                    if (!query.value().has_value()) {
                        continue;
                    }
                    const auto& manifold = *query.value();
                    auto& contact = constraints[constraint_count];
                    auto& persistence = descriptors[constraint_count];
                    contact.first_body_index = first;
                    contact.second_body_index = second;
                    contact.normal = manifold.normal;
                    contact.material = {
                        .restitution = 0.0F,
                        .static_friction = 0.8F,
                        .dynamic_friction = 0.6F,
                    };
                    contact.point_count = manifold.point_count;
                    persistence.identity = pair_identity(first, second);
                    persistence.point_count = manifold.point_count;
                    for (std::size_t point_index = 0U;
                         point_index < manifold.point_count;
                         ++point_index) {
                        const auto& point = manifold.points[point_index];
                        contact.points[point_index] = {
                            .position = point.position,
                            .penetration_depth = point.penetration_depth,
                        };
                        persistence.points[point_index] = {
                            .point_on_first = point.point_on_first,
                            .point_on_second = point.point_on_second,
                        };
                    }
                    ++constraint_count;
                }
            }

            const auto constraints_view = std::span{
                constraints.data(),
                constraint_count,
            };
            const auto descriptors_view = std::span{
                descriptors.data(),
                constraint_count,
            };
            const auto fixed_tick = clock.total_step_count() -
                frame_result.value().step_count + step + 1U;
            const auto solve =
                physics::solve_persistent_contact_constraints(
                    states,
                    mass_properties,
                    constraints_view,
                    descriptors_view,
                    fixed_tick,
                    cache,
                    solver_settings);
            REQUIRE(solve);
            metrics.last_warm_started_point_count =
                solve.value().warm_started_point_count;

            if (fixed_tick <= 480U) {
                continue;
            }
            ++metrics.final_window_ticks;
            if (solve.value().warm_started_point_count == 12U) {
                ++metrics.final_window_full_warm_start_ticks;
            }
            if (constraint_count == 3U) {
                ++metrics.final_window_three_manifold_ticks;
            }
            std::size_t point_count = 0U;
            for (std::size_t constraint_index = 0U;
                 constraint_index < constraint_count;
                 ++constraint_index) {
                point_count += constraints[constraint_index].point_count;
                for (std::size_t point_index = 0U;
                     point_index <
                         constraints[constraint_index].point_count;
                     ++point_index) {
                    metrics.maximum_penetration = std::max(
                        metrics.maximum_penetration,
                        constraints[constraint_index]
                            .points[point_index]
                            .penetration_depth);
                }
            }
            if (point_count == 12U) {
                ++metrics.final_window_twelve_point_ticks;
            }

            const auto record_post_solve_penetration =
                [&](const auto& manifold) {
                    for (std::size_t point_index = 0U;
                         point_index < manifold.point_count;
                         ++point_index) {
                        metrics.maximum_post_solve_penetration =
                            std::max(
                                metrics.maximum_post_solve_penetration,
                                manifold.points[point_index]
                                    .penetration_depth);
                    }
                };
            for (std::size_t body_index = 0U;
                 body_index < states.size();
                 ++body_index) {
                const auto query = physics::query_box_terrain_contact(
                    states[body_index],
                    collider,
                    surface);
                REQUIRE(query);
                if (query.value().has_value()) {
                    record_post_solve_penetration(*query.value());
                }
            }
            for (std::size_t first = 0U;
                 first < states.size();
                 ++first) {
                for (std::size_t second = first + 1U;
                     second < states.size();
                     ++second) {
                    const auto query = physics::query_box_box_contact(
                        states[first],
                        collider,
                        states[second],
                        collider);
                    REQUIRE(query);
                    if (query.value().has_value()) {
                        record_post_solve_penetration(*query.value());
                    }
                }
            }
            for (std::size_t body_index = 0U;
                 body_index < states.size();
                 ++body_index) {
                const auto& state = states[body_index];
                const auto offset_x = state.position.x - stack_x;
                const auto offset_z = state.position.z - stack_z;
                metrics.maximum_horizontal_drift = std::max(
                    metrics.maximum_horizontal_drift,
                    std::sqrt(
                        offset_x * offset_x +
                        offset_z * offset_z));
                metrics.maximum_height_error = std::max(
                    metrics.maximum_height_error,
                    std::abs(
                        state.position.y -
                        (0.5F + static_cast<float>(body_index))));
                metrics.maximum_linear_speed = std::max(
                    metrics.maximum_linear_speed,
                    vector_length(state.linear_velocity));
                metrics.maximum_angular_speed = std::max(
                    metrics.maximum_angular_speed,
                    vector_length(state.angular_velocity));
                const auto quaternion_w = std::clamp(
                    std::abs(state.orientation.w),
                    0.0F,
                    1.0F);
                metrics.maximum_tilt_degrees = std::max(
                    metrics.maximum_tilt_degrees,
                    2.0F * std::acos(quaternion_w) *
                        180.0F / math::pi);
            }
        }
    }

    return {
        .states = states,
        .cache = cache,
        .metrics = metrics,
        .step_count = clock.total_step_count(),
    };
}

} // namespace

TEST_CASE(
    "solid box mass properties use the exact diagonal inertia",
    "[physics][box][dynamics][mass][inertia]")
{
    using namespace shark;

    SECTION("unit cube")
    {
        constexpr physics::BoxCollider collider{
            .local_half_extents = {0.5F, 0.5F, 0.5F},
        };
        const auto result =
            physics::make_solid_box_mass_properties(1.0F, collider);
        REQUIRE(result);
        REQUIRE(result.value().collider == collider);
        REQUIRE(physics::is_valid(result.value()));
        REQUIRE(result.value().body.mass == 1.0F);
        REQUIRE(result.value().body.inverse_mass == 1.0F);
        require_float3(
            result.value().body.local_moment_of_inertia,
            {1.0F / 6.0F, 1.0F / 6.0F, 1.0F / 6.0F});
        require_float3(
            result.value().body.local_inverse_moment_of_inertia,
            {6.0F, 6.0F, 6.0F});
    }

    SECTION("rectangular box")
    {
        constexpr physics::BoxCollider collider{
            .local_half_extents = {1.0F, 2.0F, 3.0F},
        };
        const auto result =
            physics::make_solid_box_mass_properties(12.0F, collider);
        REQUIRE(result);
        require_float3(
            result.value().body.local_moment_of_inertia,
            {52.0F, 40.0F, 20.0F});
        require_float3(
            result.value().body.local_inverse_moment_of_inertia,
            {1.0F / 52.0F, 1.0F / 40.0F, 1.0F / 20.0F});
    }
}

TEST_CASE(
    "box and common mass validation reject inconsistent records",
    "[physics][box][dynamics][mass][validation]")
{
    using namespace shark;

    constexpr physics::BoxCollider collider{
        .local_half_extents = {0.5F, 0.5F, 0.5F},
    };
    REQUIRE_FALSE(
        physics::make_solid_box_mass_properties(0.0F, collider));
    REQUIRE_FALSE(physics::make_solid_box_mass_properties(
        1.0F,
        physics::BoxCollider{
            .local_half_extents = {0.5F, 0.0F, 0.5F},
        }));
    REQUIRE_FALSE(physics::make_solid_box_mass_properties(
        std::numeric_limits<float>::infinity(),
        collider));

    auto valid = physics::make_solid_box_mass_properties(
        1.0F,
        collider);
    REQUIRE(valid);
    auto inconsistent = valid.value();
    inconsistent.body.local_moment_of_inertia.x *= 2.0F;
    REQUIRE_FALSE(physics::is_valid(inconsistent));
    inconsistent = valid.value();
    inconsistent.body.local_inverse_moment_of_inertia.z *= 0.5F;
    REQUIRE_FALSE(physics::is_valid(inconsistent));
}

TEST_CASE(
    "generic angular motion conserves anisotropic world momentum",
    "[physics][box][dynamics][angular][momentum]")
{
    using namespace shark;

    constexpr physics::BoxCollider collider{
        .local_half_extents = {1.0F, 2.0F, 3.0F},
    };
    const auto mass = physics::make_solid_box_mass_properties(
        2.0F,
        collider);
    REQUIRE(mass);
    physics::RigidBodyState state{
        .orientation = axis_angle(
            {0.0F, 1.0F, 0.0F},
            math::pi / 3.0F),
        .angular_velocity = {1.0F, -2.0F, 3.0F},
    };
    const auto momentum_before = world_angular_momentum(
        state,
        mass.value().body);
    const auto result = physics::advance_rigid_body_angular_motion(
        state,
        mass.value().body,
        {},
        1.0F / 120.0F);
    REQUIRE(result);
    REQUIRE(math::is_unit(state.orientation));
    require_float3(
        world_angular_momentum(state, mass.value().body),
        momentum_before,
        0.00002F);
}

TEST_CASE(
    "generic angular integration is transactional for invalid input",
    "[physics][box][dynamics][angular][validation][immutability]")
{
    using namespace shark;

    const auto mass = physics::make_solid_box_mass_properties(
        1.0F,
        physics::BoxCollider{
            .local_half_extents = {0.5F, 0.5F, 0.5F},
        });
    REQUIRE(mass);
    physics::RigidBodyState state{
        .angular_velocity = {1.0F, 2.0F, 3.0F},
    };

    const auto require_unchanged = [&](
        const physics::RigidBodyMassProperties& properties,
        const math::Float3 torque,
        const float delta) {
        auto candidate = state;
        const auto before = candidate;
        const auto result = physics::advance_rigid_body_angular_motion(
            candidate,
            properties,
            torque,
            delta);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() == core::ErrorCode::invalid_argument);
        REQUIRE(candidate == before);
    };

    auto invalid_mass = mass.value().body;
    invalid_mass.local_inverse_moment_of_inertia.x = 0.0F;
    require_unchanged(invalid_mass, {}, 1.0F / 60.0F);
    require_unchanged(
        mass.value().body,
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
        1.0F / 60.0F);
    require_unchanged(mass.value().body, {}, 0.0F);
}

TEST_CASE(
    "varied checked sphere inertia converts to common angular motion",
    "[physics][rigid-body][mass][sphere][conversion][regression]")
{
    using namespace shark;

    constexpr float sphere_mass = 0.013467214070260525F;
    constexpr float sphere_radius = 9.95055866241455F;
    const auto sphere = physics::make_solid_sphere_mass_properties(
        sphere_mass,
        sphere_radius);
    REQUIRE(sphere);
    REQUIRE(
        sphere.value().inverse_moment_of_inertia !=
        1.0F / sphere.value().moment_of_inertia);

    const auto common = physics::to_rigid_body_mass_properties(
        sphere.value());
    REQUIRE(common);
    REQUIRE(physics::is_valid(common.value()));
    require_float3(
        common.value().local_moment_of_inertia,
        {
            sphere.value().moment_of_inertia,
            sphere.value().moment_of_inertia,
            sphere.value().moment_of_inertia,
        });
    require_float3(
        common.value().local_inverse_moment_of_inertia,
        {
            sphere.value().inverse_moment_of_inertia,
            sphere.value().inverse_moment_of_inertia,
            sphere.value().inverse_moment_of_inertia,
        });

    physics::RigidBodyState generic_state{
        .angular_velocity = {0.5F, -0.25F, 0.75F},
    };
    auto compatibility_state = generic_state;
    REQUIRE(physics::advance_rigid_body_angular_motion(
        generic_state,
        common.value(),
        {0.1F, 0.2F, -0.3F},
        1.0F / 60.0F));
    REQUIRE(physics::advance_rigid_body_angular_motion(
        compatibility_state,
        sphere.value(),
        {0.1F, 0.2F, -0.3F},
        1.0F / 60.0F));
    REQUIRE(generic_state == compatibility_state);
}

TEST_CASE(
    "tolerance-valid sphere inertia does not accumulate zero-torque drift",
    "[physics][rigid-body][mass][sphere][conversion][regression]"
    "[angular][drift]")
{
    using namespace shark;

    constexpr float sphere_mass = 0.013467214070260525F;
    constexpr float sphere_radius = 9.95055866241455F;
    const auto sphere = physics::make_solid_sphere_mass_properties(
        sphere_mass,
        sphere_radius);
    REQUIRE(sphere);
    REQUIRE(
        sphere.value().inverse_moment_of_inertia !=
        1.0F / sphere.value().moment_of_inertia);
    const auto common = physics::to_rigid_body_mass_properties(
        sphere.value());
    REQUIRE(common);

    physics::RigidBodyState state{
        .orientation = axis_angle(
            {0.0F, 1.0F, 0.0F},
            math::pi / 5.0F),
        .angular_velocity = {0.5F, -0.25F, 0.75F},
    };
    const auto initial_velocity = state.angular_velocity;
    const auto initial_momentum = world_angular_momentum(
        state,
        common.value());
    constexpr std::uint32_t fixed_tick_count = 12'000U;
    for (std::uint32_t tick = 0U;
         tick < fixed_tick_count;
         ++tick) {
        CAPTURE(tick);
        REQUIRE(physics::advance_rigid_body_angular_motion(
            state,
            common.value(),
            {},
            1.0F / 60.0F));
    }
    REQUIRE(math::is_unit(state.orientation));
    require_float3(state.angular_velocity, initial_velocity, 0.00001F);
    require_float3(
        world_angular_momentum(state, common.value()),
        initial_momentum,
        0.00001F);
}

TEST_CASE(
    "three real cubes remain stable with persistent contacts",
    "[physics][box][dynamics][stack][persistent-contact][stability]")
{
    const auto run = run_stack_schedule(60U);
    CAPTURE(
        run.metrics.maximum_horizontal_drift,
        run.metrics.maximum_height_error,
        run.metrics.maximum_penetration,
        run.metrics.maximum_post_solve_penetration,
        run.metrics.maximum_linear_speed,
        run.metrics.maximum_angular_speed,
        run.metrics.maximum_tilt_degrees,
        run.metrics.last_warm_started_point_count,
        run.states[0].position.y,
        run.states[1].position.y,
        run.states[2].position.y);
    REQUIRE(run.step_count == 600U);
    REQUIRE(run.cache.manifold_count == 3U);
    REQUIRE(run.metrics.final_window_ticks == 120U);
    REQUIRE(
        run.metrics.final_window_three_manifold_ticks == 120U);
    REQUIRE(
        run.metrics.final_window_twelve_point_ticks == 120U);
    REQUIRE(
        run.metrics.final_window_full_warm_start_ticks == 120U);
    REQUIRE(run.metrics.last_warm_started_point_count == 12U);
    REQUIRE(run.metrics.maximum_horizontal_drift <= 0.005F);
    REQUIRE(run.metrics.maximum_height_error <= 0.025F);
    // This is the penetration entering the next fixed-tick solve after
    // gravity. The post-solve measurement below isolates stabilization.
    REQUIRE(run.metrics.maximum_penetration <= 0.0125F);
    REQUIRE(run.metrics.maximum_post_solve_penetration <= 0.0125F);
    REQUIRE(run.metrics.maximum_linear_speed <= 0.02F);
    REQUIRE(run.metrics.maximum_angular_speed <= 0.03F);
    REQUIRE(run.metrics.maximum_tilt_degrees <= 1.0F);
}

TEST_CASE(
    "persistent cube stack is exact across render partitions",
    "[physics][box][dynamics][stack][persistent-contact]"
    "[fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline = run_stack_schedule(render_rates[0]);
    REQUIRE(baseline.step_count == 600U);
    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_stack_schedule(render_rate) == baseline);
    }
}
