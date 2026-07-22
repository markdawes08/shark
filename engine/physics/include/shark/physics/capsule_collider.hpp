#pragma once

#include <shark/physics/rigid_body.hpp>

#include <cmath>
#include <type_traits>

namespace shark::physics {

struct CapsuleCollider final {
    // The centerline endpoints are body position +/- this vector after the
    // body's orientation is applied. Zero is a valid sphere degeneration.
    math::Float3 local_half_segment{};
    float radius{};

    [[nodiscard]] friend bool operator==(
        const CapsuleCollider&,
        const CapsuleCollider&) noexcept = default;
};

struct CapsuleWorldSegment final {
    math::Float3 first_endpoint{};
    math::Float3 second_endpoint{};

    [[nodiscard]] friend bool operator==(
        const CapsuleWorldSegment&,
        const CapsuleWorldSegment&) noexcept = default;
};

static_assert(std::is_standard_layout_v<CapsuleCollider>);
static_assert(std::is_trivially_copyable_v<CapsuleCollider>);
static_assert(std::is_standard_layout_v<CapsuleWorldSegment>);
static_assert(std::is_trivially_copyable_v<CapsuleWorldSegment>);

[[nodiscard]] inline bool is_valid(
    const CapsuleCollider& collider) noexcept
{
    return math::is_finite(collider.local_half_segment) &&
        std::isfinite(collider.radius) &&
        collider.radius > 0.0F;
}

[[nodiscard]] core::Result<CapsuleWorldSegment>
make_capsule_world_segment(
    const RigidBodyState& state,
    const CapsuleCollider& collider);

} // namespace shark::physics
