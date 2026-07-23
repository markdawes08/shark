#include <shark/physics/body_island.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

struct PreparedBody final {
    BodySleepEntry entry{};
    bool is_new{};
    bool awake{};
    bool active_this_tick{};
};

[[nodiscard]] core::Error island_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool is_static_endpoint(
    const std::size_t body_index) noexcept
{
    return body_index == static_contact_body_index;
}

[[nodiscard]] std::size_t find_root(
    const std::array<std::size_t, body_island_capacity>& parents,
    std::size_t index) noexcept
{
    while (parents[index] != index) {
        index = parents[index];
    }
    return index;
}

void join_roots(
    std::array<std::size_t, body_island_capacity>& parents,
    const std::span<const std::uint64_t> body_ids,
    const std::size_t first,
    const std::size_t second) noexcept
{
    const auto first_root = find_root(parents, first);
    const auto second_root = find_root(parents, second);
    if (first_root == second_root) {
        return;
    }
    if (body_ids[first_root] < body_ids[second_root]) {
        parents[second_root] = first_root;
    }
    else {
        parents[first_root] = second_root;
    }
}

[[nodiscard]] bool valid_registry(
    const BodySleepRegistry& registry) noexcept
{
    if (registry.entry_count > body_sleep_registry_capacity) {
        return false;
    }
    if (!registry.has_committed_tick) {
        return registry == BodySleepRegistry{};
    }
    if (registry.last_committed_tick == 0U) {
        return false;
    }

    for (std::size_t index = 0U;
         index < registry.entry_count;
         ++index) {
        const auto& entry = registry.entries[index];
        if (entry.body_id == 0U ||
            static_cast<std::uint64_t>(
                entry.quiet_tick_count) >=
                registry.last_committed_tick ||
            (entry.sleeping && entry.quiet_tick_count == 0U) ||
            (!entry.sleeping &&
             entry.quiet_tick_count ==
                 std::numeric_limits<std::uint32_t>::max()) ||
            (index != 0U &&
             registry.entries[index - 1U].body_id >=
                 entry.body_id)) {
            return false;
        }
    }
    for (std::size_t index = registry.entry_count;
         index < body_sleep_registry_capacity;
         ++index) {
        if (registry.entries[index] != BodySleepEntry{}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_tick(
    const std::uint64_t fixed_tick,
    const BodySleepRegistry& registry) noexcept
{
    if (fixed_tick == 0U) {
        return false;
    }
    if (!registry.has_committed_tick) {
        return true;
    }
    return registry.last_committed_tick !=
            std::numeric_limits<std::uint64_t>::max() &&
        fixed_tick == registry.last_committed_tick + 1U;
}

[[nodiscard]] const BodySleepEntry* find_entry(
    const BodySleepRegistry& registry,
    const std::uint64_t body_id) noexcept
{
    for (std::size_t index = 0U;
         index < registry.entry_count;
         ++index) {
        if (registry.entries[index].body_id == body_id) {
            return &registry.entries[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool has_body_id(
    const std::span<const BodySleepInput> bodies,
    const std::uint64_t body_id) noexcept
{
    for (const auto& body : bodies) {
        if (body.body_id == body_id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_nonzero_velocity(
    const RigidBodyState& state) noexcept
{
    return state.linear_velocity.x != 0.0F ||
        state.linear_velocity.y != 0.0F ||
        state.linear_velocity.z != 0.0F ||
        state.angular_velocity.x != 0.0F ||
        state.angular_velocity.y != 0.0F ||
        state.angular_velocity.z != 0.0F;
}

[[nodiscard]] bool same_float_bits(
    const float first,
    const float second) noexcept
{
    return std::bit_cast<std::uint32_t>(first) ==
        std::bit_cast<std::uint32_t>(second);
}

[[nodiscard]] bool same_float3_bits(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    return same_float_bits(first.x, second.x) &&
        same_float_bits(first.y, second.y) &&
        same_float_bits(first.z, second.z);
}

[[nodiscard]] bool same_quaternion_bits(
    const math::Quaternion first,
    const math::Quaternion second) noexcept
{
    return same_float_bits(first.x, second.x) &&
        same_float_bits(first.y, second.y) &&
        same_float_bits(first.z, second.z) &&
        same_float_bits(first.w, second.w);
}

[[nodiscard]] bool same_state_bits(
    const RigidBodyState& first,
    const RigidBodyState& second) noexcept
{
    return same_float3_bits(first.position, second.position) &&
        same_quaternion_bits(
            first.orientation,
            second.orientation) &&
        same_float3_bits(
            first.linear_velocity,
            second.linear_velocity) &&
        same_float3_bits(
            first.angular_velocity,
            second.angular_velocity);
}

[[nodiscard]] bool same_sleeping_source_states(
    const BodyIslandSleepPlan& first,
    const BodyIslandSleepPlan& second) noexcept
{
    for (std::size_t index = 0U;
         index < body_island_capacity;
         ++index) {
        if (!same_state_bits(
                first.sleeping_source_states[index],
                second.sleeping_source_states[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_settings(
    const BodySleepSettings settings) noexcept
{
    return std::isfinite(settings.maximum_linear_speed) &&
        settings.maximum_linear_speed >= 0.0F &&
        std::isfinite(settings.maximum_angular_speed) &&
        settings.maximum_angular_speed >= 0.0F &&
        settings.minimum_quiet_ticks > 0U;
}

[[nodiscard]] double length_squared(
    const math::Float3 value) noexcept
{
    const auto x = static_cast<double>(value.x);
    const auto y = static_cast<double>(value.y);
    const auto z = static_cast<double>(value.z);
    return x * x + y * y + z * z;
}

[[nodiscard]] bool is_quiet(
    const RigidBodyState& state,
    const BodySleepSettings settings) noexcept
{
    const auto maximum_linear_speed =
        static_cast<double>(settings.maximum_linear_speed);
    const auto maximum_angular_speed =
        static_cast<double>(settings.maximum_angular_speed);
    return length_squared(state.linear_velocity) <=
            maximum_linear_speed * maximum_linear_speed &&
        length_squared(state.angular_velocity) <=
            maximum_angular_speed * maximum_angular_speed;
}

[[nodiscard]] std::uint32_t saturating_increment(
    const std::uint32_t value) noexcept
{
    return value == std::numeric_limits<std::uint32_t>::max()
        ? value
        : value + 1U;
}

[[nodiscard]] BodySleepRegistry reconcile_registry(
    const std::span<const BodySleepInput> bodies,
    const std::array<PreparedBody, body_island_capacity>& prepared,
    const std::uint64_t fixed_tick) noexcept
{
    std::array<std::size_t, body_island_capacity> body_indices{};
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        body_indices[index] = index;
    }
    std::sort(
        body_indices.begin(),
        body_indices.begin() +
            static_cast<std::ptrdiff_t>(bodies.size()),
        [&](const std::size_t first, const std::size_t second) {
            return bodies[first].body_id < bodies[second].body_id;
        });

    BodySleepRegistry result{
        .entry_count = bodies.size(),
        .last_committed_tick = fixed_tick,
        .has_committed_tick = true,
    };
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        result.entries[index] =
            prepared[body_indices[index]].entry;
    }
    return result;
}

} // namespace

core::Result<BodyIslandStep> build_body_islands(
    const std::span<const std::uint64_t> body_ids,
    const std::span<const ContactConstraint> constraints)
{
    if (body_ids.size() > body_island_capacity ||
        constraints.size() > contact_constraint_capacity) {
        return core::Result<BodyIslandStep>::failure(
            island_error(
                core::ErrorCode::invalid_argument,
                "Body islands require at most four bodies and ten "
                "constraints"));
    }
    for (std::size_t index = 0U;
         index < body_ids.size();
         ++index) {
        if (body_ids[index] == 0U) {
            return core::Result<BodyIslandStep>::failure(
                island_error(
                    core::ErrorCode::invalid_argument,
                    "Body islands require nonzero stable body IDs"));
        }
        for (std::size_t previous = 0U;
             previous < index;
             ++previous) {
            if (body_ids[previous] == body_ids[index]) {
                return core::Result<BodyIslandStep>::failure(
                    island_error(
                        core::ErrorCode::invalid_argument,
                        "Body island stable IDs must be unique"));
            }
        }
    }

    for (const auto& constraint : constraints) {
        const auto first_static =
            is_static_endpoint(constraint.first_body_index);
        const auto second_static =
            is_static_endpoint(constraint.second_body_index);
        const auto first_valid = first_static ||
            constraint.first_body_index < body_ids.size();
        const auto second_valid = second_static ||
            constraint.second_body_index < body_ids.size();
        if (!first_valid ||
            !second_valid ||
            (first_static && second_static) ||
            (!first_static &&
             !second_static &&
             constraint.first_body_index ==
                 constraint.second_body_index)) {
            return core::Result<BodyIslandStep>::failure(
                island_error(
                    core::ErrorCode::invalid_argument,
                    "Body island constraints require distinct valid "
                    "dynamic endpoints and at most one static "
                    "endpoint"));
        }
    }

    std::array<std::size_t, body_island_capacity> parents{};
    for (std::size_t index = 0U; index < body_ids.size(); ++index) {
        parents[index] = index;
    }
    for (const auto& constraint : constraints) {
        if (!is_static_endpoint(constraint.first_body_index) &&
            !is_static_endpoint(constraint.second_body_index)) {
            join_roots(
                parents,
                body_ids,
                constraint.first_body_index,
                constraint.second_body_index);
        }
    }

    std::array<std::size_t, body_island_capacity> body_order{};
    for (std::size_t index = 0U; index < body_ids.size(); ++index) {
        body_order[index] = index;
    }
    std::sort(
        body_order.begin(),
        body_order.begin() +
            static_cast<std::ptrdiff_t>(body_ids.size()),
        [&](const std::size_t first, const std::size_t second) {
            return body_ids[first] < body_ids[second];
        });

    BodyIslandStep step{
        .body_count = body_ids.size(),
        .constraint_count = constraints.size(),
    };
    step.body_island_indices.fill(invalid_body_island_index);
    step.constraint_island_indices.fill(
        invalid_body_island_index);
    std::array<std::size_t, body_island_capacity>
        root_island_indices{};
    root_island_indices.fill(invalid_body_island_index);

    for (std::size_t order_index = 0U;
         order_index < body_ids.size();
         ++order_index) {
        const auto body_index = body_order[order_index];
        const auto root = find_root(parents, body_index);
        auto island_index = root_island_indices[root];
        if (island_index == invalid_body_island_index) {
            island_index = step.island_count++;
            root_island_indices[root] = island_index;
            step.islands[island_index].minimum_body_id =
                body_ids[body_index];
        }
        auto& island = step.islands[island_index];
        island.bodies[island.body_count++] = BodyIslandMember{
            .body_id = body_ids[body_index],
            .body_index = body_index,
        };
        step.body_island_indices[body_index] = island_index;
    }

    for (std::size_t constraint_index = 0U;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        const auto first_static =
            is_static_endpoint(constraint.first_body_index);
        const auto second_static =
            is_static_endpoint(constraint.second_body_index);
        const auto representative = first_static
            ? constraint.second_body_index
            : constraint.first_body_index;
        const auto island_index =
            step.body_island_indices[representative];
        if (!first_static &&
            !second_static &&
            step.body_island_indices[
                constraint.second_body_index] != island_index) {
            return core::Result<BodyIslandStep>::failure(
                island_error(
                    core::ErrorCode::invalid_state,
                    "Body island union produced inconsistent "
                    "constraint membership"));
        }

        auto& island = step.islands[island_index];
        if (island.constraint_count >=
            contact_constraint_capacity) {
            return core::Result<BodyIslandStep>::failure(
                island_error(
                    core::ErrorCode::invalid_state,
                    "Body island constraint membership exceeded its "
                    "fixed capacity"));
        }
        island.constraint_indices[island.constraint_count++] =
            constraint_index;
        step.constraint_island_indices[constraint_index] =
            island_index;
        if (first_static || second_static) {
            ++step.static_constraint_count;
        }
        else {
            ++step.dynamic_constraint_count;
        }
    }

    return core::Result<BodyIslandStep>::success(std::move(step));
}

core::Result<BodyIslandSleepPlan> prepare_body_island_sleep(
    const std::span<const RigidBodyState> states,
    const std::span<const BodySleepInput> bodies,
    const std::span<const ContactConstraint> constraints,
    const std::uint64_t fixed_tick,
    const BodySleepRegistry& registry)
{
    if (states.size() != bodies.size() ||
        bodies.size() > body_island_capacity ||
        !valid_registry(registry) ||
        !valid_tick(fixed_tick, registry)) {
        return core::Result<BodyIslandSleepPlan>::failure(
            island_error(
                core::ErrorCode::invalid_argument,
                "Body sleep preparation requires aligned bounded "
                "state, a canonical registry, and the exact next "
                "fixed tick"));
    }
    std::array<std::uint64_t, body_island_capacity> body_ids{};
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        if (!is_valid(states[index])) {
            return core::Result<BodyIslandSleepPlan>::failure(
                island_error(
                    core::ErrorCode::invalid_argument,
                    "Body sleep preparation requires finite valid "
                    "rigid-body states"));
        }
        body_ids[index] = bodies[index].body_id;
    }

    auto island_result = build_body_islands(
        std::span<const std::uint64_t>{
            body_ids.data(),
            bodies.size(),
        },
        constraints);
    if (!island_result) {
        return core::Result<BodyIslandSleepPlan>::failure(
            std::move(island_result).error());
    }

    BodyIslandSleepPlan plan{
        .islands = std::move(island_result).value(),
        .source_registry = registry,
        .fixed_tick = fixed_tick,
    };
    for (std::size_t body_index = 0U;
         body_index < bodies.size();
         ++body_index) {
        plan.source_inputs[body_index] = bodies[body_index];
    }
    for (std::size_t constraint_index = 0U;
         constraint_index < constraints.size();
         ++constraint_index) {
        plan.source_first_body_indices[constraint_index] =
            constraints[constraint_index].first_body_index;
        plan.source_second_body_indices[constraint_index] =
            constraints[constraint_index].second_body_index;
    }
    std::array<PreparedBody, body_island_capacity> prepared{};
    for (std::size_t body_index = 0U;
         body_index < bodies.size();
         ++body_index) {
        const auto& input = bodies[body_index];
        const auto* prior = find_entry(registry, input.body_id);
        auto& current = prepared[body_index];
        if (prior == nullptr) {
            current.entry.body_id = input.body_id;
            current.is_new = true;
            current.awake = true;
            current.active_this_tick = true;
            ++plan.new_body_count;
        }
        else {
            current.entry = *prior;
            current.awake = !prior->sleeping;
        }

        const auto sleeping_motion =
            prior != nullptr &&
            prior->sleeping &&
            has_nonzero_velocity(states[body_index]);
        if (input.wake_requested ||
            !input.can_sleep ||
            sleeping_motion) {
            current.awake = true;
            current.active_this_tick = true;
        }
    }
    for (std::size_t entry_index = 0U;
         entry_index < registry.entry_count;
         ++entry_index) {
        if (!has_body_id(
                bodies,
                registry.entries[entry_index].body_id)) {
            ++plan.removed_body_count;
        }
    }

    for (std::size_t island_index = 0U;
         island_index < plan.islands.island_count;
         ++island_index) {
        const auto& island = plan.islands.islands[island_index];
        bool any_awake = false;
        bool any_sleeping = false;
        bool any_activity = false;
        for (std::size_t member_index = 0U;
             member_index < island.body_count;
             ++member_index) {
            const auto body_index =
                island.bodies[member_index].body_index;
            any_awake = any_awake || prepared[body_index].awake;
            any_sleeping =
                any_sleeping || !prepared[body_index].awake;
            any_activity =
                any_activity ||
                prepared[body_index].active_this_tick;
        }
        const auto mixed = any_awake && any_sleeping;
        const auto island_awake = any_awake;
        const auto island_active = any_activity || mixed;
        plan.island_sleeping[island_index] = !island_awake;
        plan.island_active_this_tick[island_index] =
            island_active;
        if (island_awake) {
            ++plan.awake_island_count;
        }
        else {
            ++plan.sleeping_island_count;
        }

        for (std::size_t member_index = 0U;
             member_index < island.body_count;
             ++member_index) {
            const auto body_index =
                island.bodies[member_index].body_index;
            const auto was_sleeping =
                !prepared[body_index].is_new &&
                prepared[body_index].entry.sleeping;
            if (island_awake && was_sleeping) {
                ++plan.woken_body_count;
            }
            prepared[body_index].awake = island_awake;
            prepared[body_index].active_this_tick =
                island_active;
            plan.body_awake[body_index] = island_awake;
            plan.body_active_this_tick[body_index] =
                island_active;
            if (island_awake) {
                ++plan.awake_body_count;
            }
            else {
                plan.sleeping_source_states[body_index] =
                    states[body_index];
                ++plan.sleeping_body_count;
            }
        }
        for (std::size_t constraint_offset = 0U;
             constraint_offset < island.constraint_count;
             ++constraint_offset) {
            const auto constraint_index =
                island.constraint_indices[constraint_offset];
            plan.constraint_active[constraint_index] =
                island_awake;
            if (island_awake) {
                ++plan.active_constraint_count;
            }
            else {
                ++plan.sleeping_constraint_count;
            }
        }
    }

    return core::Result<BodyIslandSleepPlan>::success(
        std::move(plan));
}

core::Result<BodyIslandSleepStep> commit_body_island_sleep(
    const std::span<RigidBodyState> states,
    const std::span<const BodySleepInput> bodies,
    const std::span<const ContactConstraint> constraints,
    const std::uint64_t fixed_tick,
    const BodyIslandSleepPlan& plan,
    BodySleepRegistry& registry,
    const BodySleepSettings settings)
{
    if (!valid_settings(settings)) {
        return core::Result<BodyIslandSleepStep>::failure(
            island_error(
                core::ErrorCode::invalid_argument,
                "Body sleep settings require finite nonnegative "
                "speed thresholds and at least one quiet tick"));
    }
    auto expected_plan = prepare_body_island_sleep(
        std::span<const RigidBodyState>{states.data(), states.size()},
        bodies,
        constraints,
        fixed_tick,
        registry);
    if (!expected_plan) {
        return core::Result<BodyIslandSleepStep>::failure(
            std::move(expected_plan).error());
    }
    if (expected_plan.value() != plan ||
        !same_sleeping_source_states(
            expected_plan.value(),
            plan)) {
        return core::Result<BodyIslandSleepStep>::failure(
            island_error(
                core::ErrorCode::invalid_state,
                "Body sleep commit no longer matches its prepared "
                "registry, inputs, topology, or sleeping motion"));
    }

    std::array<RigidBodyState, body_island_capacity>
        candidate_states{};
    std::copy(states.begin(), states.end(), candidate_states.begin());
    std::array<PreparedBody, body_island_capacity> prepared{};
    for (std::size_t body_index = 0U;
         body_index < bodies.size();
         ++body_index) {
        const auto& input = bodies[body_index];
        const auto* prior = find_entry(registry, input.body_id);
        prepared[body_index].entry = prior == nullptr
            ? BodySleepEntry{.body_id = input.body_id}
            : *prior;
        prepared[body_index].is_new = prior == nullptr;
        prepared[body_index].awake =
            plan.body_awake[body_index];
        prepared[body_index].active_this_tick =
            plan.body_active_this_tick[body_index];
    }

    BodyIslandSleepStep step{
        .islands = plan.islands,
        .fixed_tick = fixed_tick,
    };
    for (std::size_t island_index = 0U;
         island_index < plan.islands.island_count;
         ++island_index) {
        const auto& island = plan.islands.islands[island_index];
        if (plan.island_sleeping[island_index]) {
            ++step.sleeping_island_count;
            step.sleeping_body_count += island.body_count;
            continue;
        }
        if (plan.island_active_this_tick[island_index]) {
            ++step.awake_island_count;
            step.awake_body_count += island.body_count;
            for (std::size_t member_index = 0U;
                 member_index < island.body_count;
                 ++member_index) {
                const auto body_index =
                    island.bodies[member_index].body_index;
                prepared[body_index].entry.quiet_tick_count = 0U;
                prepared[body_index].entry.sleeping = false;
            }
            continue;
        }

        auto island_quiet = true;
        auto minimum_quiet_ticks =
            std::numeric_limits<std::uint32_t>::max();
        for (std::size_t member_index = 0U;
             member_index < island.body_count;
             ++member_index) {
            const auto body_index =
                island.bodies[member_index].body_index;
            island_quiet = island_quiet &&
                is_quiet(candidate_states[body_index], settings);
            minimum_quiet_ticks = std::min(
                minimum_quiet_ticks,
                prepared[body_index].entry.quiet_tick_count);
        }
        if (!island_quiet) {
            ++step.awake_island_count;
            step.awake_body_count += island.body_count;
            for (std::size_t member_index = 0U;
                 member_index < island.body_count;
                 ++member_index) {
                const auto body_index =
                    island.bodies[member_index].body_index;
                prepared[body_index].entry.quiet_tick_count = 0U;
                prepared[body_index].entry.sleeping = false;
            }
            continue;
        }

        ++step.quiet_island_count;
        const auto quiet_ticks =
            saturating_increment(minimum_quiet_ticks);
        const auto transition_to_sleep =
            quiet_ticks >= settings.minimum_quiet_ticks;
        if (transition_to_sleep) {
            ++step.newly_sleeping_island_count;
            step.newly_sleeping_body_count += island.body_count;
            ++step.sleeping_island_count;
            step.sleeping_body_count += island.body_count;
        }
        else {
            ++step.awake_island_count;
            step.awake_body_count += island.body_count;
        }
        for (std::size_t member_index = 0U;
             member_index < island.body_count;
             ++member_index) {
            const auto body_index =
                island.bodies[member_index].body_index;
            auto& entry = prepared[body_index].entry;
            entry.quiet_tick_count = quiet_ticks;
            entry.sleeping = transition_to_sleep;
            if (transition_to_sleep) {
                candidate_states[body_index].linear_velocity = {};
                candidate_states[body_index].angular_velocity = {};
            }
        }
    }

    const auto candidate_registry = reconcile_registry(
        bodies,
        prepared,
        fixed_tick);
    for (std::size_t body_index = 0U;
         body_index < states.size();
         ++body_index) {
        states[body_index] = candidate_states[body_index];
    }
    registry = candidate_registry;
    return core::Result<BodyIslandSleepStep>::success(
        std::move(step));
}

} // namespace shark::physics
