#include <shark/physics/body_island.hpp>
#include <shark/physics/persistent_contact.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace {

[[nodiscard]] shark::physics::ContactConstraint topology_constraint(
    const std::size_t first,
    const std::size_t second) noexcept
{
    return shark::physics::ContactConstraint{
        .first_body_index = first,
        .second_body_index = second,
    };
}

[[nodiscard]] shark::physics::ContactConstraint solver_constraint(
    const std::size_t first,
    const std::size_t second,
    const shark::math::Float3 normal,
    const shark::math::Float3 position = {}) noexcept
{
    return shark::physics::ContactConstraint{
        .first_body_index = first,
        .second_body_index = second,
        .normal = normal,
        .points = {{
            shark::physics::ContactConstraintPoint{
                .position = position,
            },
        }},
        .point_count = 1U,
    };
}

[[nodiscard]] shark::physics::RigidBodyState body(
    const shark::math::Float3 position = {},
    const shark::math::Float3 linear_velocity = {},
    const shark::math::Float3 angular_velocity = {}) noexcept
{
    return shark::physics::RigidBodyState{
        .position = position,
        .linear_velocity = linear_velocity,
        .angular_velocity = angular_velocity,
    };
}

template<std::size_t Count>
[[nodiscard]] std::array<shark::physics::BodySleepInput, Count>
sleep_inputs(
    const std::array<std::uint64_t, Count>& body_ids,
    const bool can_sleep = true) noexcept
{
    std::array<shark::physics::BodySleepInput, Count> result{};
    for (std::size_t index = 0U; index < Count; ++index) {
        result[index] = shark::physics::BodySleepInput{
            .body_id = body_ids[index],
            .can_sleep = can_sleep,
        };
    }
    return result;
}

template<std::size_t StateCount,
         std::size_t InputCount,
         std::size_t ConstraintCount>
[[nodiscard]] shark::physics::BodyIslandSleepStep
advance_sleep(
    std::array<shark::physics::RigidBodyState, StateCount>& states,
    const std::array<
        shark::physics::BodySleepInput,
        InputCount>& inputs,
    const std::array<
        shark::physics::ContactConstraint,
        ConstraintCount>& constraints,
    const std::uint64_t fixed_tick,
    shark::physics::BodySleepRegistry& registry,
    const shark::physics::BodySleepSettings settings = {})
{
    const auto plan = shark::physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        fixed_tick,
        registry);
    REQUIRE(plan);
    const auto commit = shark::physics::commit_body_island_sleep(
        states,
        inputs,
        constraints,
        fixed_tick,
        plan.value(),
        registry,
        settings);
    REQUIRE(commit);
    return commit.value();
}

template<std::size_t StateCount,
         std::size_t InputCount,
         std::size_t ConstraintCount>
void require_sleep_failure_is_transactional(
    std::array<shark::physics::RigidBodyState, StateCount> states,
    const std::array<
        shark::physics::BodySleepInput,
        InputCount>& inputs,
    const std::array<
        shark::physics::ContactConstraint,
        ConstraintCount>& constraints,
    const std::uint64_t fixed_tick,
    shark::physics::BodySleepRegistry registry,
    const shark::physics::BodySleepSettings settings,
    const shark::core::ErrorCode expected_code =
        shark::core::ErrorCode::invalid_argument)
{
    const auto states_before = states;
    const auto registry_before = registry;
    const auto plan = shark::physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        fixed_tick,
        registry);
    if (!plan) {
        REQUIRE(plan.error().code() == expected_code);
        REQUIRE(
            std::memcmp(
                states.data(),
                states_before.data(),
                sizeof(states)) == 0);
        REQUIRE(registry == registry_before);
        return;
    }
    const auto commit = shark::physics::commit_body_island_sleep(
        states,
        inputs,
        constraints,
        fixed_tick,
        plan.value(),
        registry,
        settings);
    REQUIRE_FALSE(commit);
    REQUIRE(commit.error().code() == expected_code);
    REQUIRE(states == states_before);
    REQUIRE(registry == registry_before);
}

void require_zero_velocities(
    const shark::physics::RigidBodyState& state)
{
    REQUIRE(state.linear_velocity == shark::math::Float3{});
    REQUIRE(state.angular_velocity == shark::math::Float3{});
}

} // namespace

TEST_CASE(
    "body islands publish exact stable topology and caller order",
    "[physics][body-island][topology][order][metrics]")
{
    using namespace shark;

    STATIC_REQUIRE(
        physics::body_island_capacity ==
        physics::contact_solver_body_capacity);
    STATIC_REQUIRE(physics::body_island_capacity == 4U);
    STATIC_REQUIRE(physics::body_sleep_registry_capacity == 4U);
    STATIC_REQUIRE(
        physics::invalid_body_island_index ==
        std::numeric_limits<std::size_t>::max());
    STATIC_REQUIRE(
        std::is_trivially_copyable_v<physics::BodyIslandStep>);
    STATIC_REQUIRE(
        std::is_trivially_copyable_v<
            physics::BodyIslandSleepPlan>);

    constexpr std::array<std::uint64_t, 4> body_ids{
        40U,
        10U,
        30U,
        20U,
    };
    constexpr auto static_body =
        physics::static_contact_body_index;
    const std::array constraints{
        topology_constraint(static_body, 0U),
        topology_constraint(2U, 0U),
        topology_constraint(3U, static_body),
        topology_constraint(1U, 3U),
        topology_constraint(0U, 2U),
    };

    const auto result =
        physics::build_body_islands(body_ids, constraints);
    REQUIRE(result);
    const auto& step = result.value();
    REQUIRE(step.body_count == 4U);
    REQUIRE(step.constraint_count == 5U);
    REQUIRE(step.island_count == 2U);
    REQUIRE(step.dynamic_constraint_count == 3U);
    REQUIRE(step.static_constraint_count == 2U);
    REQUIRE(
        step.body_island_indices ==
        std::array<std::size_t, 4>{1U, 0U, 1U, 0U});
    REQUIRE(
        step.constraint_island_indices ==
        std::array<std::size_t, 10>{
            1U,
            1U,
            0U,
            0U,
            1U,
            physics::invalid_body_island_index,
            physics::invalid_body_island_index,
            physics::invalid_body_island_index,
            physics::invalid_body_island_index,
            physics::invalid_body_island_index,
        });

    const auto& first = step.islands[0];
    REQUIRE(first.minimum_body_id == 10U);
    REQUIRE(first.body_count == 2U);
    REQUIRE(first.constraint_count == 2U);
    REQUIRE(
        first.bodies ==
        std::array<physics::BodyIslandMember, 4>{
            physics::BodyIslandMember{10U, 1U},
            physics::BodyIslandMember{20U, 3U},
            {},
            {},
        });
    REQUIRE(
        first.constraint_indices ==
        std::array<std::size_t, 10>{
            2U,
            3U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
        });

    const auto& second = step.islands[1];
    REQUIRE(second.minimum_body_id == 30U);
    REQUIRE(second.body_count == 2U);
    REQUIRE(second.constraint_count == 3U);
    REQUIRE(
        second.bodies ==
        std::array<physics::BodyIslandMember, 4>{
            physics::BodyIslandMember{30U, 2U},
            physics::BodyIslandMember{40U, 0U},
            {},
            {},
        });
    REQUIRE(
        second.constraint_indices ==
        std::array<std::size_t, 10>{
            0U,
            1U,
            4U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
        });
    REQUIRE(step.islands[2] == physics::BodyIsland{});
    REQUIRE(step.islands[3] == physics::BodyIsland{});

    for (std::size_t repeat = 0U; repeat < 64U; ++repeat) {
        CAPTURE(repeat);
        REQUIRE(
            physics::build_body_islands(
                body_ids,
                constraints).value() == step);
    }
}

TEST_CASE(
    "body islands keep static supports separate and retain isolated bodies",
    "[physics][body-island][static][isolated][capacity]")
{
    using namespace shark;

    SECTION("empty")
    {
        constexpr std::array<std::uint64_t, 0> body_ids{};
        constexpr std::array<physics::ContactConstraint, 0>
            constraints{};
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE(result);
        REQUIRE(result.value().body_count == 0U);
        REQUIRE(result.value().constraint_count == 0U);
        REQUIRE(result.value().island_count == 0U);
        REQUIRE(
            result.value().islands ==
            std::array<physics::BodyIsland, 4>{});
        REQUIRE(
            result.value().body_island_indices ==
            std::array<std::size_t, 4>{
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
            });
        REQUIRE(
            result.value().constraint_island_indices ==
            std::array<std::size_t, 10>{
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
                physics::invalid_body_island_index,
            });
    }

    SECTION("four isolated bodies are sorted by stable ID")
    {
        constexpr std::array<std::uint64_t, 4> body_ids{
            40U,
            10U,
            30U,
            20U,
        };
        constexpr std::array<physics::ContactConstraint, 0>
            constraints{};
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE(result);
        REQUIRE(result.value().island_count == 4U);
        constexpr std::array<std::uint64_t, 4> expected_ids{
            10U,
            20U,
            30U,
            40U,
        };
        constexpr std::array<std::size_t, 4> expected_slots{
            1U,
            3U,
            2U,
            0U,
        };
        for (std::size_t island_index = 0U;
             island_index < expected_ids.size();
             ++island_index) {
            const auto& island =
                result.value().islands[island_index];
            REQUIRE(island.minimum_body_id ==
                expected_ids[island_index]);
            REQUIRE(island.body_count == 1U);
            REQUIRE(island.constraint_count == 0U);
            REQUIRE(
                island.bodies[0] ==
                physics::BodyIslandMember{
                    expected_ids[island_index],
                    expected_slots[island_index],
                });
        }
    }

    SECTION("shared static world never joins bodies")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{
            20U,
            10U,
        };
        constexpr auto static_body =
            physics::static_contact_body_index;
        const std::array constraints{
            topology_constraint(static_body, 0U),
            topology_constraint(static_body, 1U),
        };
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE(result);
        REQUIRE(result.value().island_count == 2U);
        REQUIRE(result.value().dynamic_constraint_count == 0U);
        REQUIRE(result.value().static_constraint_count == 2U);
        REQUIRE(result.value().islands[0].minimum_body_id == 10U);
        REQUIRE(result.value().islands[1].minimum_body_id == 20U);
        REQUIRE(
            result.value().constraint_island_indices[0] == 1U);
        REQUIRE(
            result.value().constraint_island_indices[1] == 0U);
    }

    SECTION("full constraint capacity is retained")
    {
        constexpr std::array<std::uint64_t, 4> body_ids{
            4U,
            3U,
            2U,
            1U,
        };
        std::array<
            physics::ContactConstraint,
            physics::contact_constraint_capacity>
            constraints{};
        for (std::size_t index = 0U;
             index < constraints.size();
             ++index) {
            constraints[index] = topology_constraint(
                index % 3U,
                index % 3U + 1U);
        }
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE(result);
        REQUIRE(result.value().island_count == 1U);
        REQUIRE(result.value().constraint_count == 10U);
        REQUIRE(result.value().dynamic_constraint_count == 10U);
        REQUIRE(result.value().islands[0].body_count == 4U);
        REQUIRE(result.value().islands[0].constraint_count == 10U);
        for (std::size_t index = 0U;
             index < constraints.size();
             ++index) {
            REQUIRE(
                result.value()
                    .islands[0]
                    .constraint_indices[index] == index);
        }
    }
}

TEST_CASE(
    "body island membership survives slot and edge permutations",
    "[physics][body-island][determinism][permutation][cycle]")
{
    using namespace shark;

    constexpr std::array<std::uint64_t, 4> canonical_ids{
        10U,
        20U,
        30U,
        40U,
    };
    constexpr std::array<std::array<std::uint64_t, 4>, 4>
        permutations{{
            {10U, 20U, 30U, 40U},
            {40U, 10U, 30U, 20U},
            {20U, 40U, 10U, 30U},
            {30U, 20U, 40U, 10U},
        }};
    constexpr std::array<std::pair<std::uint64_t, std::uint64_t>, 5>
        id_edges{{
            {10U, 20U},
            {20U, 30U},
            {30U, 40U},
            {40U, 10U},
            {20U, 10U},
        }};

    for (const auto& body_ids : permutations) {
        std::array<physics::ContactConstraint, id_edges.size()>
            constraints{};
        for (std::size_t edge_index = 0U;
             edge_index < id_edges.size();
             ++edge_index) {
            const auto first = static_cast<std::size_t>(
                std::find(
                    body_ids.begin(),
                    body_ids.end(),
                    id_edges[edge_index].first) -
                body_ids.begin());
            const auto second = static_cast<std::size_t>(
                std::find(
                    body_ids.begin(),
                    body_ids.end(),
                    id_edges[edge_index].second) -
                body_ids.begin());
            constraints[edge_index] =
                edge_index % 2U == 0U
                ? topology_constraint(first, second)
                : topology_constraint(second, first);
        }
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE(result);
        REQUIRE(result.value().island_count == 1U);
        REQUIRE(result.value().islands[0].body_count == 4U);
        REQUIRE(result.value().islands[0].constraint_count == 5U);
        for (std::size_t member_index = 0U;
             member_index < canonical_ids.size();
             ++member_index) {
            REQUIRE(
                result.value()
                    .islands[0]
                    .bodies[member_index]
                    .body_id == canonical_ids[member_index]);
        }
        for (std::size_t constraint_index = 0U;
             constraint_index < constraints.size();
             ++constraint_index) {
            REQUIRE(
                result.value()
                    .islands[0]
                    .constraint_indices[constraint_index] ==
                constraint_index);
        }
    }
}

TEST_CASE(
    "body islands reject invalid topology without touching inputs",
    "[physics][body-island][validation][transaction]")
{
    using namespace shark;

    const auto require_invalid = [](
        const auto& body_ids,
        const auto& constraints) {
        const auto ids_before = body_ids;
        const auto constraints_before = constraints;
        const auto result =
            physics::build_body_islands(body_ids, constraints);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(body_ids == ids_before);
        REQUIRE(constraints == constraints_before);
    };

    SECTION("zero ID")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{1U, 0U};
        constexpr std::array<physics::ContactConstraint, 0>
            constraints{};
        require_invalid(body_ids, constraints);
    }
    SECTION("duplicate ID")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{7U, 7U};
        constexpr std::array<physics::ContactConstraint, 0>
            constraints{};
        require_invalid(body_ids, constraints);
    }
    SECTION("too many bodies")
    {
        constexpr std::array<std::uint64_t, 5> body_ids{
            1U,
            2U,
            3U,
            4U,
            5U,
        };
        constexpr std::array<physics::ContactConstraint, 0>
            constraints{};
        require_invalid(body_ids, constraints);
    }
    SECTION("too many constraints")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{1U, 2U};
        std::array<physics::ContactConstraint, 11> constraints{};
        for (auto& constraint : constraints) {
            constraint = topology_constraint(0U, 1U);
        }
        require_invalid(body_ids, constraints);
    }
    SECTION("out-of-range endpoint")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{1U, 2U};
        const std::array constraints{
            topology_constraint(0U, 2U),
        };
        require_invalid(body_ids, constraints);
    }
    SECTION("dynamic self contact")
    {
        constexpr std::array<std::uint64_t, 2> body_ids{1U, 2U};
        const std::array constraints{
            topology_constraint(1U, 1U),
        };
        require_invalid(body_ids, constraints);
    }
    SECTION("both endpoints static")
    {
        constexpr std::array<std::uint64_t, 1> body_ids{1U};
        constexpr auto static_body =
            physics::static_contact_body_index;
        const std::array constraints{
            topology_constraint(static_body, static_body),
        };
        require_invalid(body_ids, constraints);
    }
}

TEST_CASE(
    "body sleep uses exact quiet thresholds and excludes activity ticks",
    "[physics][body-island][sleep][threshold][activity][default]")
{
    using namespace shark;

    constexpr std::array<std::uint64_t, 1> body_ids{10U};
    auto inputs = sleep_inputs(body_ids);
    constexpr std::array<physics::ContactConstraint, 0> constraints{};
    std::array states{
        body(
            {},
            {0.05F, 0.0F, 0.0F},
            {0.05F, 0.0F, 0.0F}),
    };
    physics::BodySleepRegistry registry{};
    const physics::BodySleepSettings short_settings{
        .maximum_linear_speed = 0.05F,
        .maximum_angular_speed = 0.05F,
        .minimum_quiet_ticks = 3U,
    };

    const auto initial_state = states[0];
    const auto tick_one = advance_sleep(
        states,
        inputs,
        constraints,
        1U,
        registry,
        short_settings);
    REQUIRE(tick_one.quiet_island_count == 0U);
    REQUIRE(tick_one.newly_sleeping_body_count == 0U);
    REQUIRE(states[0] == initial_state);
    REQUIRE(registry.entry_count == 1U);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[0].quiet_tick_count == 0U);
    REQUIRE_FALSE(registry.entries[0].sleeping);

    const auto tick_two = advance_sleep(
        states,
        inputs,
        constraints,
        2U,
        registry,
        short_settings);
    REQUIRE(tick_two.quiet_island_count == 1U);
    REQUIRE(registry.entries[0].quiet_tick_count == 1U);
    REQUIRE(states[0] == initial_state);

    states[0].linear_velocity.x = std::nextafter(
        short_settings.maximum_linear_speed,
        std::numeric_limits<float>::infinity());
    const auto above_linear = states[0];
    const auto tick_three = advance_sleep(
        states,
        inputs,
        constraints,
        3U,
        registry,
        short_settings);
    REQUIRE(tick_three.quiet_island_count == 0U);
    REQUIRE(registry.entries[0].quiet_tick_count == 0U);
    REQUIRE(states[0] == above_linear);

    states[0].linear_velocity = {};
    states[0].angular_velocity.x = std::nextafter(
        short_settings.maximum_angular_speed,
        std::numeric_limits<float>::infinity());
    const auto above_angular = states[0];
    const auto tick_four = advance_sleep(
        states,
        inputs,
        constraints,
        4U,
        registry,
        short_settings);
    REQUIRE(tick_four.quiet_island_count == 0U);
    REQUIRE(registry.entries[0].quiet_tick_count == 0U);
    REQUIRE(states[0] == above_angular);

    states[0].linear_velocity = {0.05F, 0.0F, 0.0F};
    states[0].angular_velocity = {0.05F, 0.0F, 0.0F};
    const auto quiet_state = states[0];
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        5U,
        registry,
        short_settings).quiet_island_count == 1U);
    REQUIRE(registry.entries[0].quiet_tick_count == 1U);
    REQUIRE(states[0] == quiet_state);
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        6U,
        registry,
        short_settings).quiet_island_count == 1U);
    REQUIRE(registry.entries[0].quiet_tick_count == 2U);
    REQUIRE(states[0] == quiet_state);
    const auto sleeping = advance_sleep(
        states,
        inputs,
        constraints,
        7U,
        registry,
        short_settings);
    REQUIRE(sleeping.newly_sleeping_island_count == 1U);
    REQUIRE(sleeping.newly_sleeping_body_count == 1U);
    REQUIRE(sleeping.awake_body_count == 0U);
    REQUIRE(sleeping.sleeping_body_count == 1U);
    REQUIRE(registry.entries[0].quiet_tick_count == 3U);
    REQUIRE(registry.entries[0].sleeping);
    require_zero_velocities(states[0]);

    const auto frozen_state = states;
    const auto frozen_registry = registry;
    for (std::uint64_t tick = 8U; tick < 136U; ++tick) {
        CAPTURE(tick);
        const auto step = advance_sleep(
            states,
            inputs,
            constraints,
            tick,
            registry,
            short_settings);
        REQUIRE(step.newly_sleeping_body_count == 0U);
        REQUIRE(states == frozen_state);
        REQUIRE(registry == physics::BodySleepRegistry{
            .entries = frozen_registry.entries,
            .entry_count = frozen_registry.entry_count,
            .last_committed_tick = tick,
            .has_committed_tick = true,
        });
    }

    SECTION("default requires 60 quiet ticks after initial activity")
    {
        std::array default_states{
            body({}, {0.01F, 0.0F, 0.0F}),
        };
        physics::BodySleepRegistry default_registry{};
        REQUIRE(advance_sleep(
            default_states,
            inputs,
            constraints,
            1U,
            default_registry).newly_sleeping_body_count == 0U);
        for (std::uint64_t tick = 2U; tick <= 60U; ++tick) {
            CAPTURE(tick);
            REQUIRE(advance_sleep(
                default_states,
                inputs,
                constraints,
                tick,
                default_registry).newly_sleeping_body_count == 0U);
        }
        REQUIRE(
            default_registry.entries[0].quiet_tick_count == 59U);
        REQUIRE_FALSE(default_registry.entries[0].sleeping);
        const auto transition = advance_sleep(
            default_states,
            inputs,
            constraints,
            61U,
            default_registry);
        REQUIRE(transition.newly_sleeping_body_count == 1U);
        REQUIRE(
            default_registry.entries[0].quiet_tick_count == 60U);
        REQUIRE(default_registry.entries[0].sleeping);
        require_zero_velocities(default_states[0]);
    }
}

TEST_CASE(
    "body sleep transitions and wakes whole dynamic islands",
    "[physics][body-island][sleep][wake][propagation][can-sleep]")
{
    using namespace shark;

    constexpr std::array<std::uint64_t, 3> body_ids{
        30U,
        10U,
        20U,
    };
    auto inputs = sleep_inputs(body_ids);
    const std::array constraints{
        topology_constraint(0U, 2U),
        topology_constraint(2U, 1U),
    };
    std::array states{
        body({}, {0.01F, 0.0F, 0.0F}),
        body({}, {}, {0.0F, 0.01F, 0.0F}),
        body(),
    };
    const physics::BodySleepSettings settings{
        .maximum_linear_speed = 0.05F,
        .maximum_angular_speed = 0.05F,
        .minimum_quiet_ticks = 1U,
    };
    physics::BodySleepRegistry registry{};

    const auto new_bodies = advance_sleep(
        states,
        inputs,
        constraints,
        1U,
        registry,
        settings);
    REQUIRE(new_bodies.newly_sleeping_body_count == 0U);
    for (std::size_t index = 0U; index < registry.entry_count; ++index) {
        REQUIRE(registry.entries[index].quiet_tick_count == 0U);
        REQUIRE_FALSE(registry.entries[index].sleeping);
    }
    const auto first_sleep = advance_sleep(
        states,
        inputs,
        constraints,
        2U,
        registry,
        settings);
    REQUIRE(first_sleep.newly_sleeping_island_count == 1U);
    REQUIRE(first_sleep.newly_sleeping_body_count == 3U);
    REQUIRE(first_sleep.sleeping_body_count == 3U);
    REQUIRE(first_sleep.sleeping_island_count == 1U);
    for (const auto& state : states) {
        require_zero_velocities(state);
    }

    const auto sleeping_plan =
        physics::prepare_body_island_sleep(
            states,
            inputs,
            constraints,
            3U,
            registry);
    REQUIRE(sleeping_plan);
    REQUIRE(sleeping_plan.value().active_constraint_count == 0U);
    REQUIRE(sleeping_plan.value().sleeping_constraint_count == 2U);
    REQUIRE(
        sleeping_plan.value().constraint_active ==
        std::array<bool, 10>{});

    inputs[1].wake_requested = true;
    const auto wake_plan = physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        3U,
        registry);
    REQUIRE(wake_plan);
    REQUIRE(wake_plan.value().woken_body_count == 3U);
    REQUIRE(wake_plan.value().awake_body_count == 3U);
    REQUIRE(wake_plan.value().sleeping_body_count == 0U);
    REQUIRE(wake_plan.value().awake_island_count == 1U);
    REQUIRE(wake_plan.value().active_constraint_count == 2U);
    REQUIRE(
        wake_plan.value().body_active_this_tick ==
        std::array<bool, 4>{true, true, true, false});
    const auto wake_commit =
        physics::commit_body_island_sleep(
            states,
            inputs,
            constraints,
            3U,
            wake_plan.value(),
            registry,
            settings);
    REQUIRE(wake_commit);
    REQUIRE(wake_commit.value().newly_sleeping_body_count == 0U);
    for (std::size_t index = 0U; index < registry.entry_count; ++index) {
        REQUIRE(registry.entries[index].quiet_tick_count == 0U);
        REQUIRE_FALSE(registry.entries[index].sleeping);
    }

    inputs[1].wake_requested = false;
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        4U,
        registry,
        settings).newly_sleeping_body_count == 3U);

    inputs[2].can_sleep = false;
    const auto forced_awake = physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        5U,
        registry);
    REQUIRE(forced_awake);
    REQUIRE(forced_awake.value().woken_body_count == 3U);
    REQUIRE(forced_awake.value().awake_body_count == 3U);
    REQUIRE(forced_awake.value().active_constraint_count == 2U);
    const auto forced_commit =
        physics::commit_body_island_sleep(
            states,
            inputs,
            constraints,
            5U,
            forced_awake.value(),
            registry,
            settings);
    REQUIRE(forced_commit);
    REQUIRE(forced_commit.value().newly_sleeping_body_count == 0U);
    for (std::size_t index = 0U; index < registry.entry_count; ++index) {
        REQUIRE(registry.entries[index].quiet_tick_count == 0U);
        REQUIRE_FALSE(registry.entries[index].sleeping);
    }

    inputs[2].can_sleep = true;
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        6U,
        registry,
        settings).newly_sleeping_body_count == 3U);

    states[0].linear_velocity = {0.1F, 0.0F, 0.0F};
    const auto moving_wake = physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        7U,
        registry);
    REQUIRE(moving_wake);
    REQUIRE(moving_wake.value().woken_body_count == 3U);
    REQUIRE(moving_wake.value().awake_body_count == 3U);
    REQUIRE(moving_wake.value().body_active_this_tick[0]);
    REQUIRE(moving_wake.value().body_active_this_tick[1]);
    REQUIRE(moving_wake.value().body_active_this_tick[2]);
    const auto moving_state = states;
    const auto moving_commit =
        physics::commit_body_island_sleep(
            states,
            inputs,
            constraints,
            7U,
            moving_wake.value(),
            registry,
            settings);
    REQUIRE(moving_commit);
    REQUIRE(states == moving_state);
    REQUIRE(moving_commit.value().newly_sleeping_body_count == 0U);
}

TEST_CASE(
    "explicit support removal wakes only its dynamic island",
    "[physics][body-island][sleep][wake][static][topology]")
{
    using namespace shark;

    constexpr auto static_body =
        physics::static_contact_body_index;
    constexpr std::array<std::uint64_t, 4> body_ids{
        10U,
        20U,
        30U,
        40U,
    };
    auto inputs = sleep_inputs(body_ids);
    const std::array initial_constraints{
        topology_constraint(static_body, 0U),
        topology_constraint(0U, 1U),
        topology_constraint(static_body, 2U),
        topology_constraint(2U, 3U),
    };
    const std::array removed_support_constraints{
        topology_constraint(0U, 1U),
        topology_constraint(static_body, 2U),
        topology_constraint(2U, 3U),
    };
    std::array states{body(), body(), body(), body()};
    constexpr physics::BodySleepSettings settings{
        .minimum_quiet_ticks = 1U,
    };
    physics::BodySleepRegistry registry{};
    REQUIRE(advance_sleep(
        states,
        inputs,
        initial_constraints,
        1U,
        registry,
        settings).newly_sleeping_body_count == 0U);
    REQUIRE(advance_sleep(
        states,
        inputs,
        initial_constraints,
        2U,
        registry,
        settings).newly_sleeping_body_count == 4U);

    const auto no_request = physics::prepare_body_island_sleep(
        states,
        inputs,
        removed_support_constraints,
        3U,
        registry);
    REQUIRE(no_request);
    REQUIRE(no_request.value().woken_body_count == 0U);
    REQUIRE(no_request.value().awake_body_count == 0U);
    REQUIRE(no_request.value().sleeping_body_count == 4U);
    REQUIRE(no_request.value().active_constraint_count == 0U);
    REQUIRE(no_request.value().sleeping_constraint_count == 3U);

    inputs[0].wake_requested = true;
    const auto requested = physics::prepare_body_island_sleep(
        states,
        inputs,
        removed_support_constraints,
        3U,
        registry);
    REQUIRE(requested);
    REQUIRE(requested.value().woken_body_count == 2U);
    REQUIRE(requested.value().awake_body_count == 2U);
    REQUIRE(requested.value().sleeping_body_count == 2U);
    REQUIRE(requested.value().awake_island_count == 1U);
    REQUIRE(requested.value().sleeping_island_count == 1U);
    REQUIRE(requested.value().active_constraint_count == 1U);
    REQUIRE(requested.value().sleeping_constraint_count == 2U);
    REQUIRE(
        requested.value().constraint_active ==
        std::array<bool, 10>{
            true,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        });
    const auto commit = physics::commit_body_island_sleep(
        states,
        inputs,
        removed_support_constraints,
        3U,
        requested.value(),
        registry,
        settings);
    REQUIRE(commit);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[1].body_id == 20U);
    REQUIRE(registry.entries[2].body_id == 30U);
    REQUIRE(registry.entries[3].body_id == 40U);
    REQUIRE(registry.entries[0].quiet_tick_count == 0U);
    REQUIRE(registry.entries[1].quiet_tick_count == 0U);
    REQUIRE_FALSE(registry.entries[0].sleeping);
    REQUIRE_FALSE(registry.entries[1].sleeping);
    REQUIRE(registry.entries[2].sleeping);
    REQUIRE(registry.entries[3].sleeping);
}

TEST_CASE(
    "body sleep registry follows stable generations and sorted IDs",
    "[physics][body-island][sleep][identity][generation][registry]")
{
    using namespace shark;

    std::array<std::uint64_t, 2> body_ids{20U, 10U};
    auto inputs = sleep_inputs(body_ids);
    const std::array constraints{
        topology_constraint(0U, 1U),
    };
    std::array states{body(), body()};
    const physics::BodySleepSettings settings{
        .minimum_quiet_ticks = 1U,
    };
    physics::BodySleepRegistry registry{};

    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        1U,
        registry,
        settings).newly_sleeping_body_count == 0U);
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        2U,
        registry,
        settings).newly_sleeping_body_count == 2U);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[1].body_id == 20U);
    REQUIRE(registry.entries[0].sleeping);
    REQUIRE(registry.entries[1].sleeping);

    body_ids[0] = 30U;
    inputs = sleep_inputs(body_ids);
    const auto generation_plan =
        physics::prepare_body_island_sleep(
            states,
            inputs,
            constraints,
            3U,
            registry);
    REQUIRE(generation_plan);
    REQUIRE(generation_plan.value().new_body_count == 1U);
    REQUIRE(generation_plan.value().removed_body_count == 1U);
    REQUIRE(generation_plan.value().woken_body_count == 1U);
    REQUIRE(generation_plan.value().awake_body_count == 2U);
    REQUIRE(generation_plan.value().body_active_this_tick[0]);
    REQUIRE(generation_plan.value().body_active_this_tick[1]);
    const auto generation_commit =
        physics::commit_body_island_sleep(
            states,
            inputs,
            constraints,
            3U,
            generation_plan.value(),
            registry,
            settings);
    REQUIRE(generation_commit);
    REQUIRE(registry.entry_count == 2U);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[1].body_id == 30U);
    REQUIRE_FALSE(registry.entries[0].sleeping);
    REQUIRE_FALSE(registry.entries[1].sleeping);
    REQUIRE(registry.entries[0].quiet_tick_count == 0U);
    REQUIRE(registry.entries[1].quiet_tick_count == 0U);

    std::array one_state{states[0]};
    std::array one_input{inputs[0]};
    one_input[0].wake_requested = true;
    constexpr std::array<physics::ContactConstraint, 0>
        no_constraints{};
    const auto removal_plan =
        physics::prepare_body_island_sleep(
            one_state,
            one_input,
            no_constraints,
            4U,
            registry);
    REQUIRE(removal_plan);
    REQUIRE(removal_plan.value().new_body_count == 0U);
    REQUIRE(removal_plan.value().removed_body_count == 1U);
    const auto removal_commit =
        physics::commit_body_island_sleep(
            one_state,
            one_input,
            no_constraints,
            4U,
            removal_plan.value(),
            registry,
            settings);
    REQUIRE(removal_commit);
    REQUIRE(registry.entry_count == 1U);
    REQUIRE(registry.entries[0].body_id == 30U);
    REQUIRE(registry.entries[1] == physics::BodySleepEntry{});
}

TEST_CASE(
    "body sleep histories follow stable IDs across execution slots",
    "[physics][body-island][sleep][identity][slot][registry]")
{
    using namespace shark;

    std::array<std::uint64_t, 2> body_ids{20U, 10U};
    auto inputs = sleep_inputs(body_ids);
    constexpr std::array<physics::ContactConstraint, 0>
        constraints{};
    std::array states{body(), body()};
    constexpr physics::BodySleepSettings settings{
        .minimum_quiet_ticks = 10U,
    };
    physics::BodySleepRegistry registry{};

    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        1U,
        registry,
        settings).newly_sleeping_body_count == 0U);
    inputs[0].can_sleep = false;
    REQUIRE(advance_sleep(
        states,
        inputs,
        constraints,
        2U,
        registry,
        settings).newly_sleeping_body_count == 0U);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[0].quiet_tick_count == 1U);
    REQUIRE(registry.entries[1].body_id == 20U);
    REQUIRE(registry.entries[1].quiet_tick_count == 0U);

    std::swap(states[0], states[1]);
    std::swap(body_ids[0], body_ids[1]);
    inputs = sleep_inputs(body_ids);
    const auto plan = physics::prepare_body_island_sleep(
        states,
        inputs,
        constraints,
        3U,
        registry);
    REQUIRE(plan);
    REQUIRE(plan.value().new_body_count == 0U);
    REQUIRE(plan.value().removed_body_count == 0U);
    REQUIRE(plan.value().woken_body_count == 0U);
    const auto commit = physics::commit_body_island_sleep(
        states,
        inputs,
        constraints,
        3U,
        plan.value(),
        registry,
        settings);
    REQUIRE(commit);
    REQUIRE(registry.entries[0].body_id == 10U);
    REQUIRE(registry.entries[0].quiet_tick_count == 2U);
    REQUIRE(registry.entries[1].body_id == 20U);
    REQUIRE(registry.entries[1].quiet_tick_count == 1U);
}

TEST_CASE(
    "body sleep validation and plan verification are transactional",
    "[physics][body-island][sleep][validation][transaction][plan]")
{
    using namespace shark;

    constexpr std::array<std::uint64_t, 2> body_ids{10U, 20U};
    const auto valid_inputs = sleep_inputs(body_ids);
    const std::array constraints{
        topology_constraint(0U, 1U),
    };
    const std::array valid_states{body(), body()};

    const auto require_prepare_failure = [](
        const auto& states,
        const auto& inputs,
        const auto& constraints,
        const std::uint64_t fixed_tick,
        const physics::BodySleepRegistry& registry) {
        const auto states_before = states;
        const auto inputs_before = inputs;
        const auto constraints_before = constraints;
        const auto registry_before = registry;
        const auto result = physics::prepare_body_island_sleep(
            states,
            inputs,
            constraints,
            fixed_tick,
            registry);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(
            std::memcmp(
                states.data(),
                states_before.data(),
                sizeof(states)) == 0);
        REQUIRE(inputs == inputs_before);
        REQUIRE(constraints == constraints_before);
        REQUIRE(registry == registry_before);
    };

    SECTION("body and sleep-input spans align")
    {
        constexpr std::array<physics::BodySleepInput, 1> inputs{
            physics::BodySleepInput{.body_id = 10U},
        };
        require_prepare_failure(
            valid_states,
            inputs,
            constraints,
            1U,
            {});
    }
    SECTION("sleep IDs are nonzero and unique")
    {
        auto inputs = valid_inputs;
        inputs[1].body_id = 0U;
        require_prepare_failure(
            valid_states,
            inputs,
            constraints,
            1U,
            {});
        inputs[1].body_id = 10U;
        require_prepare_failure(
            valid_states,
            inputs,
            constraints,
            1U,
            {});
    }
    SECTION("every body state is valid")
    {
        auto states = valid_states;
        states[1].position.x =
            std::numeric_limits<float>::quiet_NaN();
        require_prepare_failure(
            states,
            valid_inputs,
            constraints,
            1U,
            {});
    }
    SECTION("fixed tick zero is invalid")
    {
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            0U,
            {});
    }
    SECTION("an empty registry may begin at a later fixed tick")
    {
        auto states = valid_states;
        physics::BodySleepRegistry registry{};
        const auto plan = physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            37U,
            registry);
        REQUIRE(plan);
        const auto commit = physics::commit_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            37U,
            plan.value(),
            registry);
        REQUIRE(commit);
        REQUIRE(registry.last_committed_tick == 37U);
        REQUIRE(registry.has_committed_tick);
        REQUIRE(physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            38U,
            registry));
        REQUIRE_FALSE(physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            39U,
            registry));
    }
    SECTION("registry active prefix is canonical")
    {
        physics::BodySleepRegistry registry{
            .entries = {
                physics::BodySleepEntry{.body_id = 20U},
                physics::BodySleepEntry{.body_id = 10U},
            },
            .entry_count = 2U,
            .last_committed_tick = 1U,
            .has_committed_tick = true,
        };
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);

        registry.entries[0].body_id = 10U;
        registry.entries[1].body_id = 10U;
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);

        registry.entries[1].body_id = 20U;
        registry.entries[2].body_id = 30U;
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);
    }
    SECTION("registry sleep counters represent possible states")
    {
        physics::BodySleepRegistry registry{
            .entries = {
                physics::BodySleepEntry{
                    .body_id = 10U,
                    .quiet_tick_count = 0U,
                    .sleeping = true,
                },
            },
            .entry_count = 1U,
            .last_committed_tick = 1U,
            .has_committed_tick = true,
        };
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);

        registry.entries[0].quiet_tick_count =
            std::numeric_limits<std::uint32_t>::max();
        registry.entries[0].sleeping = false;
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);

        registry.entries[0].quiet_tick_count = 1U;
        registry.last_committed_tick = 1U;
        require_prepare_failure(
            valid_states,
            valid_inputs,
            constraints,
            2U,
            registry);
    }
    SECTION("settings require finite nonnegative thresholds")
    {
        constexpr std::array invalid_settings{
            physics::BodySleepSettings{
                .maximum_linear_speed = -1.0F,
            },
            physics::BodySleepSettings{
                .maximum_linear_speed =
                    std::numeric_limits<float>::infinity(),
            },
            physics::BodySleepSettings{
                .maximum_angular_speed =
                    std::numeric_limits<float>::quiet_NaN(),
            },
            physics::BodySleepSettings{
                .minimum_quiet_ticks = 0U,
            },
        };
        for (const auto settings : invalid_settings) {
            require_sleep_failure_is_transactional(
                valid_states,
                valid_inputs,
                constraints,
                1U,
                {},
                settings);
        }
    }
    SECTION("commit rejects every changed public plan surface")
    {
        auto states = valid_states;
        physics::BodySleepRegistry registry{};
        const auto plan = physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            1U,
            registry);
        REQUIRE(plan);
        std::array tampered_plans{
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
            plan.value(),
        };
        ++tampered_plans[0].awake_body_count;
        tampered_plans[1].source_registry.has_committed_tick = true;
        tampered_plans[2].body_awake[0] = false;
        tampered_plans[3].constraint_active[0] = false;
        tampered_plans[4]
            .islands
            .body_island_indices[0] =
            physics::invalid_body_island_index;
        tampered_plans[5]
            .sleeping_source_states[0]
            .position
            .x = 1.0F;
        tampered_plans[6].source_inputs[0].wake_requested = true;
        tampered_plans[7].source_first_body_indices[0] = 1U;
        tampered_plans[8].source_second_body_indices[0] = 0U;
        for (const auto& tampered : tampered_plans) {
            auto candidate_states = states;
            auto candidate_registry = registry;
            const auto states_before = candidate_states;
            const auto registry_before = candidate_registry;
            const auto commit = physics::commit_body_island_sleep(
                candidate_states,
                valid_inputs,
                constraints,
                1U,
                tampered,
                candidate_registry);
            REQUIRE_FALSE(commit);
            REQUIRE(
                commit.error().code() ==
                core::ErrorCode::invalid_state);
            REQUIRE(candidate_states == states_before);
            REQUIRE(candidate_registry == registry_before);
        }
    }
    SECTION("commit rejects topology rewiring inside one island")
    {
        constexpr std::array<std::uint64_t, 3> ids{
            10U,
            20U,
            30U,
        };
        const auto inputs = sleep_inputs(ids);
        std::array states{body(), body(), body()};
        const std::array original_constraints{
            topology_constraint(0U, 1U),
            topology_constraint(1U, 2U),
        };
        const std::array rewired_constraints{
            topology_constraint(0U, 1U),
            topology_constraint(0U, 2U),
        };
        physics::BodySleepRegistry registry{};
        const auto plan = physics::prepare_body_island_sleep(
            states,
            inputs,
            original_constraints,
            1U,
            registry);
        REQUIRE(plan);
        REQUIRE(plan.value().islands.island_count == 1U);
        REQUIRE(
            physics::build_body_islands(
                ids,
                rewired_constraints).value().island_count == 1U);
        const auto states_before = states;
        const auto registry_before = registry;
        const auto commit = physics::commit_body_island_sleep(
            states,
            inputs,
            rewired_constraints,
            1U,
            plan.value(),
            registry);
        REQUIRE_FALSE(commit);
        REQUIRE(
            commit.error().code() ==
            core::ErrorCode::invalid_state);
        REQUIRE(states == states_before);
        REQUIRE(registry == registry_before);
    }
    SECTION("commit rejects registry changes between phases")
    {
        auto states = valid_states;
        physics::BodySleepRegistry registry{};
        const auto plan = physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            1U,
            registry);
        REQUIRE(plan);
        registry.last_committed_tick = 7U;
        registry.has_committed_tick = true;
        const auto states_before = states;
        const auto registry_before = registry;
        const auto commit = physics::commit_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            1U,
            plan.value(),
            registry);
        REQUIRE_FALSE(commit);
        REQUIRE(
            commit.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(states == states_before);
        REQUIRE(registry == registry_before);
    }
    SECTION("commits require the exact next tick")
    {
        auto states = valid_states;
        physics::BodySleepRegistry registry{};
        REQUIRE(advance_sleep(
            states,
            valid_inputs,
            constraints,
            1U,
            registry).fixed_tick == 1U);
        const auto plan = physics::prepare_body_island_sleep(
            states,
            valid_inputs,
            constraints,
            3U,
            registry);
        REQUIRE_FALSE(plan);
        REQUIRE(
            plan.error().code() ==
            core::ErrorCode::invalid_argument);
        const auto states_before = states;
        const auto registry_before = registry;
        REQUIRE(states == states_before);
        REQUIRE(registry == registry_before);
    }
    SECTION("planned sleepers cannot start moving before commit")
    {
        std::array states{body()};
        constexpr std::array<std::uint64_t, 1> ids{10U};
        const auto inputs = sleep_inputs(ids);
        constexpr std::array<physics::ContactConstraint, 0>
            no_constraints{};
        const physics::BodySleepSettings settings{
            .minimum_quiet_ticks = 1U,
        };
        physics::BodySleepRegistry registry{};
        REQUIRE(advance_sleep(
            states,
            inputs,
            no_constraints,
            1U,
            registry,
            settings).newly_sleeping_body_count == 0U);
        REQUIRE(advance_sleep(
            states,
            inputs,
            no_constraints,
            2U,
            registry,
            settings).newly_sleeping_body_count == 1U);
        const auto plan = physics::prepare_body_island_sleep(
            states,
            inputs,
            no_constraints,
            3U,
            registry);
        REQUIRE(plan);
        REQUIRE_FALSE(plan.value().body_awake[0]);
        states[0].linear_velocity = {1.0F, 0.0F, 0.0F};
        const auto states_before = states;
        const auto registry_before = registry;
        const auto commit = physics::commit_body_island_sleep(
            states,
            inputs,
            no_constraints,
            3U,
            plan.value(),
            registry,
            settings);
        REQUIRE_FALSE(commit);
        REQUIRE(
            commit.error().code() ==
            core::ErrorCode::invalid_state);
        REQUIRE(states == states_before);
        REQUIRE(registry == registry_before);
    }
    SECTION("planned sleeper pose cannot change before commit")
    {
        std::array sleeping_states{body()};
        constexpr std::array<std::uint64_t, 1> ids{10U};
        const auto inputs = sleep_inputs(ids);
        constexpr std::array<physics::ContactConstraint, 0>
            no_constraints{};
        const physics::BodySleepSettings settings{
            .minimum_quiet_ticks = 1U,
        };
        physics::BodySleepRegistry sleeping_registry{};
        REQUIRE(advance_sleep(
            sleeping_states,
            inputs,
            no_constraints,
            1U,
            sleeping_registry,
            settings).newly_sleeping_body_count == 0U);
        REQUIRE(advance_sleep(
            sleeping_states,
            inputs,
            no_constraints,
            2U,
            sleeping_registry,
            settings).newly_sleeping_body_count == 1U);

        for (std::size_t mutation = 0U; mutation < 2U; ++mutation) {
            CAPTURE(mutation);
            auto states = sleeping_states;
            auto registry = sleeping_registry;
            const auto plan =
                physics::prepare_body_island_sleep(
                    states,
                    inputs,
                    no_constraints,
                    3U,
                    registry);
            REQUIRE(plan);
            REQUIRE_FALSE(plan.value().body_awake[0]);
            REQUIRE(
                plan.value().sleeping_source_states[0] ==
                states[0]);
            REQUIRE(
                plan.value().sleeping_source_states[1] ==
                physics::RigidBodyState{});
            if (mutation == 0U) {
                states[0].position.x = 1.0F;
            }
            else {
                states[0].orientation =
                    math::Quaternion{1.0F, 0.0F, 0.0F, 0.0F};
            }
            const auto states_before = states;
            const auto registry_before = registry;
            const auto commit =
                physics::commit_body_island_sleep(
                    states,
                    inputs,
                    no_constraints,
                    3U,
                    plan.value(),
                    registry,
                    settings);
            REQUIRE_FALSE(commit);
            REQUIRE(
                commit.error().code() ==
                core::ErrorCode::invalid_state);
            REQUIRE(states == states_before);
            REQUIRE(registry == registry_before);
        }
    }
}

TEST_CASE(
    "all-awake island masking preserves direct contact solving exactly",
    "[physics][body-island][awake][contact-constraint][identity]")
{
    using namespace shark;

    constexpr auto static_body =
        physics::static_contact_body_index;
    const std::array constraints{
        solver_constraint(
            static_body,
            0U,
            {0.0F, 1.0F, 0.0F}),
        solver_constraint(
            0U,
            1U,
            {0.0F, 1.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}),
        solver_constraint(
            1U,
            2U,
            {0.0F, 1.0F, 0.0F},
            {0.0F, 2.0F, 0.0F}),
    };
    const std::array masses{
        physics::ContactBodyMassProperties{.inverse_mass = 1.0F},
        physics::ContactBodyMassProperties{.inverse_mass = 1.0F},
        physics::ContactBodyMassProperties{.inverse_mass = 1.0F},
    };
    const std::array initial_states{
        body({}, {0.0F, -1.0F, 0.0F}),
        body({0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
        body({0.0F, 2.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
    };
    constexpr std::array<std::uint64_t, 3> body_ids{
        30U,
        10U,
        20U,
    };
    const auto inputs = sleep_inputs(body_ids, false);
    auto solver_settings = physics::ContactSolverSettings{};
    solver_settings.velocity_iterations = 1U;

    auto direct_states = initial_states;
    const auto direct = physics::solve_contact_constraints(
        direct_states,
        masses,
        constraints,
        solver_settings);
    REQUIRE(direct);

    auto island_states = initial_states;
    const auto states_before_plan = island_states;
    physics::BodySleepRegistry registry{};
    const auto plan = physics::prepare_body_island_sleep(
        island_states,
        inputs,
        constraints,
        1U,
        registry);
    REQUIRE(plan);
    REQUIRE(
        std::memcmp(
            island_states.data(),
            states_before_plan.data(),
            sizeof(island_states)) == 0);
    REQUIRE(
        plan.value().sleeping_source_states ==
        std::array<physics::RigidBodyState, 4>{});
    REQUIRE(plan.value().active_constraint_count == 3U);
    REQUIRE(
        plan.value().constraint_active ==
        std::array<bool, 10>{
            true,
            true,
            true,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        });
    REQUIRE(
        plan.value().islands.islands[0].constraint_indices[0] ==
        0U);
    REQUIRE(
        plan.value().islands.islands[0].constraint_indices[1] ==
        1U);
    REQUIRE(
        plan.value().islands.islands[0].constraint_indices[2] ==
        2U);
    const auto island_solve = physics::solve_contact_constraints(
        island_states,
        masses,
        constraints,
        solver_settings);
    REQUIRE(island_solve);
    REQUIRE(island_solve.value() == direct.value());
    REQUIRE(island_states == direct_states);
    const auto expected_states = island_states;
    const auto commit = physics::commit_body_island_sleep(
        island_states,
        inputs,
        constraints,
        1U,
        plan.value(),
        registry);
    REQUIRE(commit);
    REQUIRE(island_states == expected_states);
    REQUIRE(
        std::memcmp(
            island_states.data(),
            expected_states.data(),
            sizeof(island_states)) == 0);
    REQUIRE(commit.value().newly_sleeping_body_count == 0U);
}

TEST_CASE(
    "all-awake island masking preserves persistent solving exactly",
    "[physics][body-island][awake][persistent-contact][identity]")
{
    using namespace shark;

    constexpr auto static_body =
        physics::static_contact_body_index;
    const std::array constraints{
        solver_constraint(
            static_body,
            0U,
            {0.0F, 1.0F, 0.0F}),
        solver_constraint(
            0U,
            1U,
            {0.0F, 1.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}),
    };
    const std::array descriptors{
        physics::ContactPersistenceDescriptor{
            .identity = {
                .first_endpoint = 1U,
                .second_endpoint = 10U,
                .first_shape = 1U,
                .second_shape = 1U,
            },
            .points = {{
                physics::ContactPersistencePoint{},
            }},
            .point_count = 1U,
        },
        physics::ContactPersistenceDescriptor{
            .identity = {
                .first_endpoint = 10U,
                .second_endpoint = 20U,
                .first_shape = 1U,
                .second_shape = 1U,
            },
            .points = {{
                physics::ContactPersistencePoint{
                    .point_on_first = {0.0F, 1.0F, 0.0F},
                    .point_on_second = {0.0F, 1.0F, 0.0F},
                },
            }},
            .point_count = 1U,
        },
    };
    const std::array masses{
        physics::ContactBodyMassProperties{.inverse_mass = 1.0F},
        physics::ContactBodyMassProperties{.inverse_mass = 1.0F},
    };
    const std::array initial_states{
        body({}, {0.0F, -1.0F, 0.0F}),
        body({0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}),
    };
    constexpr std::array<std::uint64_t, 2> body_ids{10U, 20U};
    const auto inputs = sleep_inputs(body_ids, false);

    auto direct_states = initial_states;
    physics::ContactManifoldCache direct_cache{};
    const auto direct =
        physics::solve_persistent_contact_constraints(
            direct_states,
            masses,
            constraints,
            descriptors,
            1U,
            direct_cache);
    REQUIRE(direct);

    auto island_states = initial_states;
    const auto states_before_plan = island_states;
    physics::ContactManifoldCache island_cache{};
    physics::BodySleepRegistry registry{};
    const auto plan = physics::prepare_body_island_sleep(
        island_states,
        inputs,
        constraints,
        1U,
        registry);
    REQUIRE(plan);
    REQUIRE(
        std::memcmp(
            island_states.data(),
            states_before_plan.data(),
            sizeof(island_states)) == 0);
    REQUIRE(plan.value().active_constraint_count == 2U);
    const auto island_solve =
        physics::solve_persistent_contact_constraints(
            island_states,
            masses,
            constraints,
            descriptors,
            1U,
            island_cache);
    REQUIRE(island_solve);
    REQUIRE(island_solve.value() == direct.value());
    REQUIRE(island_cache == direct_cache);
    REQUIRE(island_states == direct_states);
    const auto expected_states = island_states;
    const auto commit = physics::commit_body_island_sleep(
        island_states,
        inputs,
        constraints,
        1U,
        plan.value(),
        registry);
    REQUIRE(commit);
    REQUIRE(island_states == expected_states);
    REQUIRE(
        std::memcmp(
            island_states.data(),
            expected_states.data(),
            sizeof(island_states)) == 0);
}

namespace {

struct SleepScheduleRun final {
    std::array<shark::physics::RigidBodyState, 2> states{};
    shark::physics::BodySleepRegistry registry{};
    shark::physics::BodyIslandSleepStep last_step{};
    std::array<std::uint64_t, 8> transition_ticks{};
    std::size_t transition_count{};
    std::uint64_t fixed_step_count{};
    std::uint64_t new_body_count{};
    std::uint64_t removed_body_count{};
    std::uint64_t woken_body_count{};
    std::uint64_t newly_sleeping_body_count{};
    std::uint64_t newly_sleeping_island_count{};

    [[nodiscard]] friend bool operator==(
        const SleepScheduleRun&,
        const SleepScheduleRun&) noexcept = default;
};

[[nodiscard]] SleepScheduleRun run_sleep_schedule(
    const std::uint32_t render_rate_hz)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    SleepScheduleRun run{
        .states = {body(), body()},
    };
    const std::array constraints{
        topology_constraint(0U, 1U),
    };
    constexpr physics::BodySleepSettings settings{
        .maximum_linear_speed = 0.05F,
        .maximum_angular_speed = 0.05F,
        .minimum_quiet_ticks = 3U,
    };
    auto previous_timestamp = std::chrono::nanoseconds{0};
    constexpr std::uint32_t duration_seconds = 1U;
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;

    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;

        for (std::uint32_t step_index = 0U;
             step_index < frame_result.value().step_count;
             ++step_index) {
            const auto fixed_tick = clock.total_step_count() -
                frame_result.value().step_count + step_index + 1U;
            const std::array<std::uint64_t, 2> body_ids{
                10U,
                fixed_tick < 20U ? 20U : 30U,
            };
            auto inputs = sleep_inputs(body_ids);
            inputs[0].wake_requested = fixed_tick == 5U;
            inputs[1].can_sleep = fixed_tick != 10U;
            const auto plan =
                physics::prepare_body_island_sleep(
                    run.states,
                    inputs,
                    constraints,
                    fixed_tick,
                    run.registry);
            REQUIRE(plan);
            const auto commit =
                physics::commit_body_island_sleep(
                    run.states,
                    inputs,
                    constraints,
                    fixed_tick,
                    plan.value(),
                    run.registry,
                    settings);
            REQUIRE(commit);
            run.last_step = commit.value();
            run.new_body_count += plan.value().new_body_count;
            run.removed_body_count +=
                plan.value().removed_body_count;
            run.woken_body_count += plan.value().woken_body_count;
            run.newly_sleeping_body_count +=
                commit.value().newly_sleeping_body_count;
            run.newly_sleeping_island_count +=
                commit.value().newly_sleeping_island_count;

            const auto transition =
                plan.value().woken_body_count != 0U ||
                commit.value().newly_sleeping_body_count != 0U ||
                (fixed_tick != 1U &&
                 plan.value().new_body_count != 0U);
            if (transition) {
                REQUIRE(
                    run.transition_count <
                    run.transition_ticks.size());
                run.transition_ticks[run.transition_count] =
                    fixed_tick;
                ++run.transition_count;
            }
        }
    }
    run.fixed_step_count = clock.total_step_count();
    return run;
}

} // namespace

TEST_CASE(
    "body sleep transitions are exact across render partitions",
    "[physics][body-island][sleep][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline = run_sleep_schedule(render_rates[0]);
    REQUIRE(baseline.fixed_step_count == 60U);
    REQUIRE(baseline.transition_count == 7U);
    REQUIRE(
        baseline.transition_ticks ==
        std::array<std::uint64_t, 8>{
            4U,
            5U,
            8U,
            10U,
            13U,
            20U,
            23U,
            0U,
        });
    REQUIRE(baseline.new_body_count == 3U);
    REQUIRE(baseline.removed_body_count == 1U);
    REQUIRE(baseline.woken_body_count == 5U);
    REQUIRE(baseline.newly_sleeping_body_count == 8U);
    REQUIRE(baseline.newly_sleeping_island_count == 4U);
    REQUIRE(baseline.registry.entry_count == 2U);
    REQUIRE(baseline.registry.entries[0].body_id == 10U);
    REQUIRE(baseline.registry.entries[1].body_id == 30U);
    REQUIRE(baseline.registry.entries[0].quiet_tick_count == 3U);
    REQUIRE(baseline.registry.entries[1].quiet_tick_count == 3U);
    REQUIRE(baseline.registry.entries[0].sleeping);
    REQUIRE(baseline.registry.entries[1].sleeping);
    REQUIRE(baseline.registry.last_committed_tick == 60U);
    REQUIRE(baseline.registry.has_committed_tick);
    REQUIRE(baseline.last_step.awake_body_count == 0U);
    REQUIRE(baseline.last_step.sleeping_body_count == 2U);
    REQUIRE(baseline.last_step.awake_island_count == 0U);
    REQUIRE(baseline.last_step.sleeping_island_count == 1U);
    require_zero_velocities(baseline.states[0]);
    require_zero_velocities(baseline.states[1]);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_sleep_schedule(render_rate) == baseline);
    }
}
