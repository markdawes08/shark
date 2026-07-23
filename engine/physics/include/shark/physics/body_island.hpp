#pragma once

#include <shark/physics/contact_constraint.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace shark::physics {

inline constexpr std::size_t body_island_capacity =
    contact_solver_body_capacity;
inline constexpr std::size_t body_sleep_registry_capacity =
    body_island_capacity;
inline constexpr std::size_t invalid_body_island_index =
    std::numeric_limits<std::size_t>::max();

struct BodyIslandMember final {
    std::uint64_t body_id{};
    std::size_t body_index{};

    [[nodiscard]] friend bool operator==(
        const BodyIslandMember&,
        const BodyIslandMember&) noexcept = default;
};

struct BodyIsland final {
    std::uint64_t minimum_body_id{};
    std::array<BodyIslandMember, body_island_capacity> bodies{};
    std::array<
        std::size_t,
        contact_constraint_capacity>
        constraint_indices{};
    std::size_t body_count{};
    std::size_t constraint_count{};

    [[nodiscard]] friend bool operator==(
        const BodyIsland&,
        const BodyIsland&) noexcept = default;
};

struct BodyIslandStep final {
    std::array<BodyIsland, body_island_capacity> islands{};
    std::array<
        std::size_t,
        body_island_capacity>
        body_island_indices{
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
        };
    std::array<
        std::size_t,
        contact_constraint_capacity>
        constraint_island_indices{
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
        };
    std::size_t body_count{};
    std::size_t constraint_count{};
    std::size_t island_count{};
    std::size_t dynamic_constraint_count{};
    std::size_t static_constraint_count{};

    [[nodiscard]] friend bool operator==(
        const BodyIslandStep&,
        const BodyIslandStep&) noexcept = default;
};

struct BodySleepInput final {
    // This nonzero generation-bearing ID owns sleep history. Reusing a body
    // execution slot for a new lifetime requires a new ID.
    std::uint64_t body_id{};
    bool can_sleep{true};
    bool wake_requested{};

    [[nodiscard]] friend bool operator==(
        const BodySleepInput&,
        const BodySleepInput&) noexcept = default;
};

struct BodySleepEntry final {
    std::uint64_t body_id{};
    std::uint32_t quiet_tick_count{};
    bool sleeping{};

    [[nodiscard]] friend bool operator==(
        const BodySleepEntry&,
        const BodySleepEntry&) noexcept = default;
};

struct BodySleepRegistry final {
    // The active prefix is compact and strictly sorted by stable body ID.
    std::array<
        BodySleepEntry,
        body_sleep_registry_capacity>
        entries{};
    std::size_t entry_count{};
    std::uint64_t last_committed_tick{};
    bool has_committed_tick{};

    [[nodiscard]] friend bool operator==(
        const BodySleepRegistry&,
        const BodySleepRegistry&) noexcept = default;
};

struct BodySleepSettings final {
    float maximum_linear_speed{0.05F};
    float maximum_angular_speed{0.05F};
    std::uint32_t minimum_quiet_ticks{60U};

    [[nodiscard]] friend bool operator==(
        const BodySleepSettings&,
        const BodySleepSettings&) noexcept = default;
};

struct BodyIslandSleepPlan final {
    BodyIslandStep islands{};
    // Exact source snapshot used to reject registry changes between phases.
    BodySleepRegistry source_registry{};
    std::array<BodySleepInput, body_island_capacity> source_inputs{};
    std::array<
        std::size_t,
        contact_constraint_capacity>
        source_first_body_indices{
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
        };
    std::array<
        std::size_t,
        contact_constraint_capacity>
        source_second_body_indices{
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
            invalid_body_island_index,
        };
    // Populated only for planned sleepers so commit detects any state change.
    std::array<
        RigidBodyState,
        body_island_capacity>
        sleeping_source_states{};
    std::array<bool, body_island_capacity> body_awake{};
    std::array<bool, body_island_capacity> body_active_this_tick{};
    std::array<bool, body_island_capacity> island_sleeping{};
    std::array<bool, body_island_capacity> island_active_this_tick{};
    std::array<
        bool,
        contact_constraint_capacity>
        constraint_active{};
    std::uint64_t fixed_tick{};
    std::size_t new_body_count{};
    std::size_t removed_body_count{};
    std::size_t woken_body_count{};
    std::size_t awake_body_count{};
    std::size_t sleeping_body_count{};
    std::size_t awake_island_count{};
    std::size_t sleeping_island_count{};
    std::size_t active_constraint_count{};
    std::size_t sleeping_constraint_count{};

    [[nodiscard]] friend bool operator==(
        const BodyIslandSleepPlan&,
        const BodyIslandSleepPlan&) noexcept = default;
};

struct BodyIslandSleepStep final {
    BodyIslandStep islands{};
    std::uint64_t fixed_tick{};
    std::size_t quiet_island_count{};
    std::size_t newly_sleeping_island_count{};
    std::size_t newly_sleeping_body_count{};
    std::size_t awake_body_count{};
    std::size_t sleeping_body_count{};
    std::size_t awake_island_count{};
    std::size_t sleeping_island_count{};

    [[nodiscard]] friend bool operator==(
        const BodyIslandSleepStep&,
        const BodyIslandSleepStep&) noexcept = default;
};

static_assert(std::is_standard_layout_v<BodyIslandMember>);
static_assert(std::is_trivially_copyable_v<BodyIslandMember>);
static_assert(std::is_standard_layout_v<BodyIsland>);
static_assert(std::is_trivially_copyable_v<BodyIsland>);
static_assert(std::is_standard_layout_v<BodyIslandStep>);
static_assert(std::is_trivially_copyable_v<BodyIslandStep>);
static_assert(std::is_standard_layout_v<BodySleepInput>);
static_assert(std::is_trivially_copyable_v<BodySleepInput>);
static_assert(std::is_standard_layout_v<BodySleepEntry>);
static_assert(std::is_trivially_copyable_v<BodySleepEntry>);
static_assert(std::is_standard_layout_v<BodySleepRegistry>);
static_assert(std::is_trivially_copyable_v<BodySleepRegistry>);
static_assert(std::is_standard_layout_v<BodySleepSettings>);
static_assert(std::is_trivially_copyable_v<BodySleepSettings>);
static_assert(std::is_standard_layout_v<BodyIslandSleepPlan>);
static_assert(std::is_trivially_copyable_v<BodyIslandSleepPlan>);
static_assert(std::is_standard_layout_v<BodyIslandSleepStep>);
static_assert(std::is_trivially_copyable_v<BodyIslandSleepStep>);

// Builds connected components from exact contact constraints. Dynamic
// endpoints connect; the shared static sentinel is never a graph vertex.
// Islands and their members are sorted by stable body ID, while constraint
// indices retain caller order. Only endpoint topology is validated here;
// callers must validate the complete contact batch before masking a solve.
[[nodiscard]] core::Result<BodyIslandStep> build_body_islands(
    std::span<const std::uint64_t> body_ids,
    std::span<const ContactConstraint> constraints);

// Reconciles the stable-ID registry and propagates wake requests before a
// solve without mutating states or registry. The returned constraint mask
// retains original order. Before building current exact contacts, a caller
// integrates any body that is not a same-ID registered sleeper, is explicitly
// waking, or cannot sleep. An unchanged sleeper stays held. The caller then
// prepares this plan, solves only its active constraints, and commits the same
// fixed tick below.
//
// Force, torque, impulse, pose/shape/mass changes, contact topology changes,
// support removal, and moving static geometry require explicit wake requests.
// A nonzero velocity on an already sleeping body is detected automatically.
[[nodiscard]] core::Result<BodyIslandSleepPlan>
prepare_body_island_sleep(
    std::span<const RigidBodyState> states,
    std::span<const BodySleepInput> bodies,
    std::span<const ContactConstraint> constraints,
    std::uint64_t fixed_tick,
    const BodySleepRegistry& registry);

// Recreates and verifies the pre-solve plan against the post-solve states,
// then advances one consecutive fixed tick atomically. An activity tick never
// counts as quiet. Awake body bytes remain untouched unless an entire island
// reaches the sleep threshold, when only its two velocity vectors become
// exact zero. Failure leaves both states and registry unchanged.
[[nodiscard]] core::Result<BodyIslandSleepStep>
commit_body_island_sleep(
    std::span<RigidBodyState> states,
    std::span<const BodySleepInput> bodies,
    std::span<const ContactConstraint> constraints,
    std::uint64_t fixed_tick,
    const BodyIslandSleepPlan& plan,
    BodySleepRegistry& registry,
    BodySleepSettings settings = {});

} // namespace shark::physics
