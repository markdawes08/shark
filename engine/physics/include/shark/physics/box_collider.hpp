#pragma once

#include <shark/physics/rigid_body.hpp>
#include <shark/terrain/height_tile.hpp>

#include <array>
#include <cmath>
#include <type_traits>

namespace shark::physics {

struct BoxCollider final {
    // Positive distances from the body origin to each local face.
    math::Float3 local_half_extents{};

    [[nodiscard]] friend bool operator==(
        const BoxCollider&,
        const BoxCollider&) noexcept = default;
};

struct BoxWorldGeometry final {
    math::Float3 center{};
    std::array<math::Float3, 3> axes{};
    math::Float3 half_extents{};
    std::array<math::Float3, 8> vertices{};
    terrain::Bounds3 bounds{};

    [[nodiscard]] friend bool operator==(
        const BoxWorldGeometry&,
        const BoxWorldGeometry&) noexcept = default;
};

static_assert(std::is_standard_layout_v<BoxCollider>);
static_assert(std::is_trivially_copyable_v<BoxCollider>);
static_assert(std::is_standard_layout_v<BoxWorldGeometry>);
static_assert(std::is_trivially_copyable_v<BoxWorldGeometry>);

[[nodiscard]] inline bool is_valid(
    const BoxCollider& collider) noexcept
{
    return math::is_finite(collider.local_half_extents) &&
        collider.local_half_extents.x > 0.0F &&
        collider.local_half_extents.y > 0.0F &&
        collider.local_half_extents.z > 0.0F;
}

// Produces the exact finite world-space axes, corners, and inclusive bounds
// used by every box query. Vertex order is the XYZ sign-bit order.
[[nodiscard]] core::Result<BoxWorldGeometry> make_box_world_geometry(
    const RigidBodyState& state,
    const BoxCollider& collider);

} // namespace shark::physics
