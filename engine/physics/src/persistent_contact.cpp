#include <shark/physics/persistent_contact.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr double unit_vector_tolerance = 0.00001;
inline constexpr double cached_tangent_plane_tolerance = 0.0001;
inline constexpr std::size_t point_match_candidate_capacity =
    contact_points_per_constraint * contact_points_per_constraint;

struct Double3 final {
    double x{};
    double y{};
    double z{};
};

struct CurrentContactPoint final {
    math::Float3 first_anchor{};
    math::Float3 second_anchor{};
};

struct CurrentContact final {
    ContactManifoldIdentity identity{};
    math::Float3 normal{};
    std::array<
        CurrentContactPoint,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};
    bool first_endpoint_static{};
    bool second_endpoint_static{};
};

struct PointMatchCandidate final {
    double combined_distance_squared{};
    std::size_t current_point_index{};
    std::size_t cached_point_index{};
};

struct EndpointBinding final {
    std::uint64_t identity{};
    std::size_t body_index{};
    bool is_static{};
};

[[nodiscard]] core::Error persistence_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool representable_float(const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(std::numeric_limits<float>::max());
}

[[nodiscard]] Double3 to_double(const math::Float3 value) noexcept
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] Double3 add(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z,
    };
}

[[nodiscard]] Double3 subtract(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z,
    };
}

[[nodiscard]] Double3 scale(
    const Double3 value,
    const double factor) noexcept
{
    return {
        value.x * factor,
        value.y * factor,
        value.z * factor,
    };
}

[[nodiscard]] double dot(
    const Double3 first,
    const Double3 second) noexcept
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

[[nodiscard]] Double3 cross(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x,
    };
}

[[nodiscard]] double length_squared(const Double3 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] Double3 rotate(
    const math::Quaternion orientation,
    const Double3 value) noexcept
{
    const Double3 quaternion_vector{
        static_cast<double>(orientation.x),
        static_cast<double>(orientation.y),
        static_cast<double>(orientation.z),
    };
    const auto twice_cross = scale(
        cross(quaternion_vector, value),
        2.0);
    return add(
        add(
            value,
            scale(
                twice_cross,
                static_cast<double>(orientation.w))),
        cross(quaternion_vector, twice_cross));
}

[[nodiscard]] Double3 inverse_rotate(
    const math::Quaternion orientation,
    const Double3 value) noexcept
{
    return rotate(
        math::Quaternion{
            -orientation.x,
            -orientation.y,
            -orientation.z,
            orientation.w,
        },
        value);
}

[[nodiscard]] bool store_float3(
    const Double3 value,
    math::Float3& destination) noexcept
{
    if (!representable_float(value.x) ||
        !representable_float(value.y) ||
        !representable_float(value.z)) {
        return false;
    }
    destination = {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
    return true;
}

[[nodiscard]] bool is_static_body(
    const std::size_t body_index) noexcept
{
    return body_index == static_contact_body_index;
}

[[nodiscard]] bool valid_identity(
    const ContactManifoldIdentity identity) noexcept
{
    return identity.first_endpoint != 0U &&
        identity.second_endpoint != 0U &&
        identity.first_endpoint != identity.second_endpoint;
}

[[nodiscard]] int compare_identity(
    const ContactManifoldIdentity first,
    const ContactManifoldIdentity second) noexcept
{
    if (first.first_endpoint != second.first_endpoint) {
        return first.first_endpoint < second.first_endpoint ? -1 : 1;
    }
    if (first.second_endpoint != second.second_endpoint) {
        return first.second_endpoint < second.second_endpoint ? -1 : 1;
    }
    if (first.first_shape != second.first_shape) {
        return first.first_shape < second.first_shape ? -1 : 1;
    }
    if (first.second_shape != second.second_shape) {
        return first.second_shape < second.second_shape ? -1 : 1;
    }
    return 0;
}

[[nodiscard]] bool valid_unit_normal(
    const math::Float3 normal) noexcept
{
    if (!math::is_finite(normal)) {
        return false;
    }
    const auto length = std::sqrt(length_squared(to_double(normal)));
    return std::isfinite(length) &&
        std::abs(length - 1.0) <= unit_vector_tolerance;
}

[[nodiscard]] bool valid_persistence_settings(
    const ContactPersistenceSettings settings) noexcept
{
    return std::isfinite(settings.point_match_distance) &&
        settings.point_match_distance >= 0.0F &&
        std::isfinite(settings.minimum_normal_alignment) &&
        settings.minimum_normal_alignment >= 0.0F &&
        settings.minimum_normal_alignment <= 1.0F &&
        settings.maximum_inactive_ticks <=
            maximum_contact_cache_inactive_ticks;
}

[[nodiscard]] bool valid_cached_tangent(
    const math::Float3 tangent,
    const math::Float3 normal) noexcept
{
    if (!math::is_finite(tangent)) {
        return false;
    }
    const auto tangent_double = to_double(tangent);
    const auto tangent_length = std::sqrt(
        length_squared(tangent_double));
    const auto plane_error = std::abs(
        dot(tangent_double, to_double(normal)));
    return std::isfinite(tangent_length) &&
        std::isfinite(plane_error) &&
        plane_error <= cached_tangent_plane_tolerance *
            (1.0 + tangent_length);
}

[[nodiscard]] bool valid_cache(
    const ContactManifoldCache& cache) noexcept
{
    if (cache.manifold_count > contact_manifold_cache_capacity) {
        return false;
    }
    if (!cache.has_committed_tick) {
        return cache == ContactManifoldCache{};
    }

    for (std::size_t manifold_index = 0;
         manifold_index < cache.manifold_count;
         ++manifold_index) {
        const auto& manifold = cache.manifolds[manifold_index];
        if (!valid_identity(manifold.identity) ||
            !valid_unit_normal(manifold.normal) ||
            manifold.point_count == 0U ||
            manifold.point_count > contact_points_per_constraint ||
            manifold.last_seen_tick > cache.last_committed_tick ||
            cache.last_committed_tick - manifold.last_seen_tick >
                maximum_contact_cache_inactive_ticks ||
            (manifold.first_endpoint_static &&
             manifold.second_endpoint_static) ||
            (manifold_index != 0U &&
             compare_identity(
                 cache.manifolds[manifold_index - 1U].identity,
                 manifold.identity) >= 0)) {
            return false;
        }
        for (std::size_t point_index = 0;
             point_index < manifold.point_count;
             ++point_index) {
            const auto& point = manifold.points[point_index];
            if (!math::is_finite(point.first_anchor) ||
                !math::is_finite(point.second_anchor) ||
                !std::isfinite(point.normal_impulse_magnitude) ||
                point.normal_impulse_magnitude < 0.0F ||
                !valid_cached_tangent(
                    point.tangent_impulse,
                    manifold.normal)) {
                return false;
            }
        }
        for (std::size_t point_index = manifold.point_count;
             point_index < contact_points_per_constraint;
             ++point_index) {
            if (manifold.points[point_index] != CachedContactPoint{}) {
                return false;
            }
        }
    }
    for (std::size_t manifold_index = cache.manifold_count;
         manifold_index < contact_manifold_cache_capacity;
         ++manifold_index) {
        if (cache.manifolds[manifold_index] !=
            CachedContactManifold{}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool make_anchor(
    const std::span<const RigidBodyState> states,
    const std::size_t body_index,
    const math::Float3 witness,
    math::Float3& anchor) noexcept
{
    if (is_static_body(body_index)) {
        anchor = witness;
        return math::is_finite(anchor);
    }
    if (body_index >= states.size() ||
        !is_valid(states[body_index]) ||
        !math::is_finite(witness)) {
        return false;
    }
    const auto offset = subtract(
        to_double(witness),
        to_double(states[body_index].position));
    return store_float3(
        inverse_rotate(states[body_index].orientation, offset),
        anchor);
}

[[nodiscard]] bool add_endpoint_binding(
    std::array<
        EndpointBinding,
        contact_constraint_capacity * 2U>& bindings,
    std::size_t& binding_count,
    const std::uint64_t identity,
    const std::size_t body_index) noexcept
{
    const auto endpoint_is_static = is_static_body(body_index);
    for (std::size_t index = 0; index < binding_count; ++index) {
        if (bindings[index].identity == identity) {
            return bindings[index].body_index == body_index &&
                bindings[index].is_static == endpoint_is_static;
        }
        if (!endpoint_is_static &&
            !bindings[index].is_static &&
            bindings[index].body_index == body_index) {
            return false;
        }
    }
    bindings[binding_count++] = EndpointBinding{
        .identity = identity,
        .body_index = body_index,
        .is_static = endpoint_is_static,
    };
    return true;
}

[[nodiscard]] const CachedContactManifold* find_cached_manifold(
    const ContactManifoldCache& cache,
    const ContactManifoldIdentity identity) noexcept
{
    for (std::size_t index = 0;
         index < cache.manifold_count;
         ++index) {
        const auto comparison = compare_identity(
            cache.manifolds[index].identity,
            identity);
        if (comparison == 0) {
            return &cache.manifolds[index];
        }
        if (comparison > 0) {
            break;
        }
    }
    return nullptr;
}

[[nodiscard]] double anchor_distance_squared(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    return length_squared(subtract(to_double(first), to_double(second)));
}

[[nodiscard]] bool match_candidate_less(
    const PointMatchCandidate& first,
    const PointMatchCandidate& second) noexcept
{
    if (first.combined_distance_squared !=
        second.combined_distance_squared) {
        return first.combined_distance_squared <
            second.combined_distance_squared;
    }
    if (first.current_point_index != second.current_point_index) {
        return first.current_point_index < second.current_point_index;
    }
    return first.cached_point_index < second.cached_point_index;
}

void sort_match_candidates(
    std::array<
        PointMatchCandidate,
        point_match_candidate_capacity>& candidates,
    const std::size_t candidate_count) noexcept
{
    for (std::size_t index = 1; index < candidate_count; ++index) {
        auto candidate = candidates[index];
        auto insertion = index;
        while (insertion != 0U &&
               match_candidate_less(
                   candidate,
                   candidates[insertion - 1U])) {
            candidates[insertion] = candidates[insertion - 1U];
            --insertion;
        }
        candidates[insertion] = candidate;
    }
}

void sort_cache(ContactManifoldCache& cache) noexcept
{
    for (std::size_t index = 1;
         index < cache.manifold_count;
         ++index) {
        auto manifold = cache.manifolds[index];
        auto insertion = index;
        while (insertion != 0U &&
               compare_identity(
                   manifold.identity,
                   cache.manifolds[insertion - 1U].identity) < 0) {
            cache.manifolds[insertion] =
                cache.manifolds[insertion - 1U];
            --insertion;
        }
        cache.manifolds[insertion] = manifold;
    }
}

[[nodiscard]] bool identity_is_current(
    const std::span<const CurrentContact> current_contacts,
    const ContactManifoldIdentity identity) noexcept
{
    return std::any_of(
        current_contacts.begin(),
        current_contacts.end(),
        [identity](const CurrentContact& current) {
            return current.identity == identity;
        });
}

} // namespace

core::Result<PersistentContactStep>
solve_persistent_contact_constraints(
    const std::span<RigidBodyState> states,
    const std::span<const ContactBodyMassProperties> mass_properties,
    const std::span<const ContactConstraint> constraints,
    const std::span<const ContactPersistenceDescriptor> persistence,
    const std::uint64_t fixed_tick,
    ContactManifoldCache& cache,
    const ContactSolverSettings solver_settings,
    const ContactPersistenceSettings persistence_settings)
{
    if (states.size() > contact_solver_body_capacity ||
        mass_properties.size() != states.size() ||
        constraints.size() > contact_constraint_capacity ||
        persistence.size() != constraints.size() ||
        !valid_persistence_settings(persistence_settings) ||
        !valid_cache(cache) ||
        (cache.has_committed_tick &&
         fixed_tick <= cache.last_committed_tick)) {
        return core::Result<PersistentContactStep>::failure(
            persistence_error(
                core::ErrorCode::invalid_argument,
                "Persistent contact solving requires bounded aligned "
                "input, a canonical valid cache/settings record, and "
                "a strictly newer fixed tick"));
    }

    std::array<CurrentContact, contact_constraint_capacity>
        current_contact_storage{};
    std::array<
        EndpointBinding,
        contact_constraint_capacity * 2U>
        endpoint_bindings{};
    std::size_t endpoint_binding_count = 0U;
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        const auto& descriptor = persistence[constraint_index];
        const auto first_is_static =
            is_static_body(constraint.first_body_index);
        const auto second_is_static =
            is_static_body(constraint.second_body_index);
        if (!valid_identity(descriptor.identity) ||
            !valid_unit_normal(constraint.normal) ||
            descriptor.point_count != constraint.point_count ||
            descriptor.point_count == 0U ||
            descriptor.point_count > contact_points_per_constraint ||
            (!first_is_static &&
             constraint.first_body_index >= states.size()) ||
            (!second_is_static &&
             constraint.second_body_index >= states.size()) ||
            (first_is_static && second_is_static) ||
            !add_endpoint_binding(
                endpoint_bindings,
                endpoint_binding_count,
                descriptor.identity.first_endpoint,
                constraint.first_body_index) ||
            !add_endpoint_binding(
                endpoint_bindings,
                endpoint_binding_count,
                descriptor.identity.second_endpoint,
                constraint.second_body_index)) {
            return core::Result<PersistentContactStep>::failure(
                persistence_error(
                    core::ErrorCode::invalid_argument,
                    "Persistent constraints require unique stable "
                    "ordered endpoint identities, aligned points, unit "
                    "normals, and consistent endpoint bindings"));
        }
        for (std::size_t prior = 0;
             prior < constraint_index;
             ++prior) {
            if (persistence[prior].identity == descriptor.identity) {
                return core::Result<PersistentContactStep>::failure(
                    persistence_error(
                        core::ErrorCode::invalid_argument,
                        "Current persistent manifold identities must "
                        "be unique"));
            }
        }

        auto& current = current_contact_storage[constraint_index];
        current.identity = descriptor.identity;
        current.normal = constraint.normal;
        current.point_count = descriptor.point_count;
        current.first_endpoint_static = first_is_static;
        current.second_endpoint_static = second_is_static;
        for (std::size_t point_index = 0;
             point_index < descriptor.point_count;
             ++point_index) {
            const auto& point = descriptor.points[point_index];
            if (!math::is_finite(point.point_on_first) ||
                !math::is_finite(point.point_on_second) ||
                !make_anchor(
                    states,
                    constraint.first_body_index,
                    point.point_on_first,
                    current.points[point_index].first_anchor) ||
                !make_anchor(
                    states,
                    constraint.second_body_index,
                    point.point_on_second,
                    current.points[point_index].second_anchor)) {
                return core::Result<PersistentContactStep>::failure(
                    persistence_error(
                        core::ErrorCode::invalid_argument,
                        "Persistent contact witnesses must be finite "
                        "and representable in endpoint anchor space"));
            }
        }
    }
    const auto current_contacts = std::span<const CurrentContact>{
        current_contact_storage.data(),
        constraints.size(),
    };

    std::size_t retained_absent_count = 0U;
    std::size_t expired_manifold_count = 0U;
    for (std::size_t cache_index = 0;
         cache_index < cache.manifold_count;
         ++cache_index) {
        const auto& cached = cache.manifolds[cache_index];
        const auto inactive_ticks = fixed_tick - cached.last_seen_tick;
        if (identity_is_current(current_contacts, cached.identity)) {
            if (inactive_ticks >
                static_cast<std::uint64_t>(
                    persistence_settings.maximum_inactive_ticks) +
                    1U) {
                ++expired_manifold_count;
            }
            continue;
        }
        if (inactive_ticks <=
            persistence_settings.maximum_inactive_ticks) {
            ++retained_absent_count;
        }
        else {
            ++expired_manifold_count;
        }
    }
    if (retained_absent_count + constraints.size() >
        contact_manifold_cache_capacity) {
        return core::Result<PersistentContactStep>::failure(
            persistence_error(
                core::ErrorCode::invalid_argument,
                "Persistent contact cache capacity would be exceeded"));
    }

    std::array<
        ContactConstraintWarmStart,
        contact_constraint_capacity>
        warm_start_storage{};
    std::size_t warm_started_point_count = 0U;
    const auto match_distance_squared =
        static_cast<double>(persistence_settings.point_match_distance) *
        static_cast<double>(persistence_settings.point_match_distance);
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& current = current_contact_storage[constraint_index];
        auto& warm_start = warm_start_storage[constraint_index];
        warm_start.point_count = current.point_count;
        const auto* cached = find_cached_manifold(
            cache,
            current.identity);
        if (cached == nullptr ||
            fixed_tick - cached->last_seen_tick >
                static_cast<std::uint64_t>(
                    persistence_settings.maximum_inactive_ticks) +
                    1U ||
            cached->first_endpoint_static !=
                current.first_endpoint_static ||
            cached->second_endpoint_static !=
                current.second_endpoint_static ||
            dot(
                to_double(cached->normal),
                to_double(current.normal)) <
                static_cast<double>(
                    persistence_settings.minimum_normal_alignment)) {
            continue;
        }

        std::array<
            PointMatchCandidate,
            point_match_candidate_capacity>
            candidates{};
        std::size_t candidate_count = 0U;
        for (std::size_t current_point = 0;
             current_point < current.point_count;
             ++current_point) {
            for (std::size_t cached_point = 0;
                 cached_point < cached->point_count;
                 ++cached_point) {
                const auto first_distance_squared =
                    anchor_distance_squared(
                        current.points[current_point].first_anchor,
                        cached->points[cached_point].first_anchor);
                const auto second_distance_squared =
                    anchor_distance_squared(
                        current.points[current_point].second_anchor,
                        cached->points[cached_point].second_anchor);
                if (!std::isfinite(first_distance_squared) ||
                    !std::isfinite(second_distance_squared)) {
                    return core::Result<
                        PersistentContactStep>::failure(
                            persistence_error(
                                core::ErrorCode::unavailable,
                                "Persistent anchor distance exceeded "
                                "finite range"));
                }
                if (first_distance_squared <= match_distance_squared &&
                    second_distance_squared <= match_distance_squared) {
                    candidates[candidate_count++] = PointMatchCandidate{
                        .combined_distance_squared =
                            first_distance_squared +
                            second_distance_squared,
                        .current_point_index = current_point,
                        .cached_point_index = cached_point,
                    };
                }
            }
        }
        sort_match_candidates(candidates, candidate_count);
        std::array<bool, contact_points_per_constraint>
            current_point_matched{};
        std::array<bool, contact_points_per_constraint>
            cached_point_matched{};
        for (std::size_t candidate_index = 0;
             candidate_index < candidate_count;
             ++candidate_index) {
            const auto& candidate = candidates[candidate_index];
            if (current_point_matched[candidate.current_point_index] ||
                cached_point_matched[candidate.cached_point_index]) {
                continue;
            }
            current_point_matched[candidate.current_point_index] = true;
            cached_point_matched[candidate.cached_point_index] = true;
            const auto& cached_point =
                cached->points[candidate.cached_point_index];
            auto& seed = warm_start.points[
                candidate.current_point_index];
            seed.normal_impulse_magnitude =
                cached_point.normal_impulse_magnitude;
            seed.tangent_impulse = cached_point.tangent_impulse;
            ++warm_started_point_count;
        }
    }

    std::array<RigidBodyState, contact_solver_body_capacity>
        candidate_state_storage{};
    std::copy(states.begin(), states.end(), candidate_state_storage.begin());
    auto solver_result = solve_contact_constraints(
        std::span<RigidBodyState>{
            candidate_state_storage.data(),
            states.size(),
        },
        mass_properties,
        constraints,
        std::span<const ContactConstraintWarmStart>{
            warm_start_storage.data(),
            constraints.size(),
        },
        solver_settings);
    if (!solver_result) {
        return core::Result<PersistentContactStep>::failure(
            std::move(solver_result).error());
    }
    const auto& solver_step = solver_result.value();
    if (solver_step.constraint_count != constraints.size()) {
        return core::Result<PersistentContactStep>::failure(
            persistence_error(
                core::ErrorCode::invalid_state,
                "Contact solver returned an inconsistent persistent "
                "constraint count"));
    }

    ContactManifoldCache candidate_cache{
        .last_committed_tick = fixed_tick,
        .has_committed_tick = true,
    };
    for (std::size_t cache_index = 0;
         cache_index < cache.manifold_count;
         ++cache_index) {
        const auto& cached = cache.manifolds[cache_index];
        if (identity_is_current(current_contacts, cached.identity)) {
            continue;
        }
        if (fixed_tick - cached.last_seen_tick <=
            persistence_settings.maximum_inactive_ticks) {
            candidate_cache.manifolds[candidate_cache.manifold_count++] =
                cached;
        }
    }
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& current = current_contact_storage[constraint_index];
        const auto& result = solver_step.constraints[constraint_index];
        if (result.point_count != current.point_count) {
            return core::Result<PersistentContactStep>::failure(
                persistence_error(
                    core::ErrorCode::invalid_state,
                    "Contact solver returned an inconsistent persistent "
                    "point count"));
        }
        auto& cached =
            candidate_cache.manifolds[candidate_cache.manifold_count++];
        cached.identity = current.identity;
        cached.normal = current.normal;
        cached.point_count = current.point_count;
        cached.last_seen_tick = fixed_tick;
        cached.first_endpoint_static =
            current.first_endpoint_static;
        cached.second_endpoint_static =
            current.second_endpoint_static;
        for (std::size_t point_index = 0;
             point_index < current.point_count;
             ++point_index) {
            cached.points[point_index] = CachedContactPoint{
                .first_anchor =
                    current.points[point_index].first_anchor,
                .second_anchor =
                    current.points[point_index].second_anchor,
                .normal_impulse_magnitude =
                    result.points[point_index]
                        .normal_impulse_magnitude,
                .tangent_impulse =
                    result.points[point_index].tangent_impulse,
            };
        }
    }
    sort_cache(candidate_cache);
    if (!valid_cache(candidate_cache)) {
        return core::Result<PersistentContactStep>::failure(
            persistence_error(
                core::ErrorCode::invalid_state,
                "Persistent solver produced a noncanonical cache"));
    }

    for (std::size_t body_index = 0;
         body_index < states.size();
         ++body_index) {
        states[body_index] = candidate_state_storage[body_index];
    }
    cache = candidate_cache;
    return core::Result<PersistentContactStep>::success(
        PersistentContactStep{
            .solver = solver_step,
            .warm_started_point_count = warm_started_point_count,
            .cold_started_point_count =
                constraints.empty()
                ? 0U
                : std::accumulate(
                      constraints.begin(),
                      constraints.end(),
                      std::size_t{},
                      [](const std::size_t total,
                         const ContactConstraint& constraint) {
                          return total + constraint.point_count;
                      }) -
                    warm_started_point_count,
            .expired_manifold_count = expired_manifold_count,
        });
}

} // namespace shark::physics
