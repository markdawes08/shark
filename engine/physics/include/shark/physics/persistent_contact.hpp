#pragma once

#include <shark/physics/contact_constraint.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace shark::physics {

inline constexpr std::uint32_t
    maximum_contact_cache_inactive_ticks = 2U;
inline constexpr std::size_t contact_manifold_cache_capacity =
    contact_constraint_capacity *
    (maximum_contact_cache_inactive_ticks + 1U);

struct ContactManifoldIdentity final {
    // Opaque stable endpoint IDs. Reused body slots must receive a new,
    // generation-bearing ID. One dynamic body has one ID during a tick;
    // static geometry may use multiple IDs. Field order follows the
    // constraint endpoints.
    std::uint64_t first_endpoint{};
    std::uint64_t second_endpoint{};
    std::uint32_t first_shape{};
    std::uint32_t second_shape{};

    [[nodiscard]] friend bool operator==(
        const ContactManifoldIdentity&,
        const ContactManifoldIdentity&) noexcept = default;
};

struct ContactPersistencePoint final {
    // Exact world-space endpoint witnesses supplied by the narrow phase.
    math::Float3 point_on_first{};
    math::Float3 point_on_second{};

    [[nodiscard]] friend bool operator==(
        const ContactPersistencePoint&,
        const ContactPersistencePoint&) noexcept = default;
};

struct ContactPersistenceDescriptor final {
    ContactManifoldIdentity identity{};
    std::array<
        ContactPersistencePoint,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const ContactPersistenceDescriptor&,
        const ContactPersistenceDescriptor&) noexcept = default;
};

struct ContactPersistenceSettings final {
    float point_match_distance{0.05F};
    float minimum_normal_alignment{0.95F};
    std::uint32_t maximum_inactive_ticks{1U};

    [[nodiscard]] friend bool operator==(
        const ContactPersistenceSettings&,
        const ContactPersistenceSettings&) noexcept = default;
};

struct CachedContactPoint final {
    // A dynamic endpoint uses a local-space anchor. A static endpoint uses
    // the supplied world-space witness unchanged.
    math::Float3 first_anchor{};
    math::Float3 second_anchor{};
    float normal_impulse_magnitude{};
    math::Float3 tangent_impulse{};

    [[nodiscard]] friend bool operator==(
        const CachedContactPoint&,
        const CachedContactPoint&) noexcept = default;
};

struct CachedContactManifold final {
    ContactManifoldIdentity identity{};
    math::Float3 normal{};
    std::array<
        CachedContactPoint,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};
    std::uint64_t last_seen_tick{};
    bool first_endpoint_static{};
    bool second_endpoint_static{};

    [[nodiscard]] friend bool operator==(
        const CachedContactManifold&,
        const CachedContactManifold&) noexcept = default;
};

struct ContactManifoldCache final {
    // Entries are compact and strictly lexicographically sorted by identity.
    std::array<
        CachedContactManifold,
        contact_manifold_cache_capacity>
        manifolds{};
    std::size_t manifold_count{};
    std::uint64_t last_committed_tick{};
    bool has_committed_tick{};

    [[nodiscard]] friend bool operator==(
        const ContactManifoldCache&,
        const ContactManifoldCache&) noexcept = default;
};

struct PersistentContactStep final {
    ContactSolverStep solver{};
    std::size_t warm_started_point_count{};
    std::size_t cold_started_point_count{};
    std::size_t expired_manifold_count{};

    [[nodiscard]] friend bool operator==(
        const PersistentContactStep&,
        const PersistentContactStep&) noexcept = default;
};

static_assert(std::is_standard_layout_v<ContactManifoldIdentity>);
static_assert(std::is_trivially_copyable_v<ContactManifoldIdentity>);
static_assert(std::is_standard_layout_v<ContactPersistencePoint>);
static_assert(std::is_trivially_copyable_v<ContactPersistencePoint>);
static_assert(std::is_standard_layout_v<ContactPersistenceDescriptor>);
static_assert(std::is_trivially_copyable_v<ContactPersistenceDescriptor>);
static_assert(std::is_standard_layout_v<ContactPersistenceSettings>);
static_assert(std::is_trivially_copyable_v<ContactPersistenceSettings>);
static_assert(std::is_standard_layout_v<CachedContactPoint>);
static_assert(std::is_trivially_copyable_v<CachedContactPoint>);
static_assert(std::is_standard_layout_v<CachedContactManifold>);
static_assert(std::is_trivially_copyable_v<CachedContactManifold>);
static_assert(std::is_standard_layout_v<ContactManifoldCache>);
static_assert(std::is_trivially_copyable_v<ContactManifoldCache>);
static_assert(std::is_standard_layout_v<PersistentContactStep>);
static_assert(std::is_trivially_copyable_v<PersistentContactStep>);

// Advances exactly one persistent-contact transaction for a strictly newer
// fixed simulation tick. Descriptors align one-for-one with constraints and
// supply exact endpoint witnesses for anchor matching. Cached manifolds never
// create contacts: only current constraints are solved. Empty current input
// still advances cache expiry. Any validation, capacity, or numerical failure
// leaves both states and cache unchanged.
[[nodiscard]] core::Result<PersistentContactStep>
solve_persistent_contact_constraints(
    std::span<RigidBodyState> states,
    std::span<const ContactBodyMassProperties> mass_properties,
    std::span<const ContactConstraint> constraints,
    std::span<const ContactPersistenceDescriptor> persistence,
    std::uint64_t fixed_tick,
    ContactManifoldCache& cache,
    ContactSolverSettings solver_settings = {},
    ContactPersistenceSettings persistence_settings = {});

} // namespace shark::physics
