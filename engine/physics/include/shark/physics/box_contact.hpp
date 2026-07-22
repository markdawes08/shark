#pragma once

#include <shark/physics/box_collider.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace shark::physics {

inline constexpr std::size_t box_contact_manifold_capacity = 4;

enum class BoxBoxSatFeature : std::uint8_t {
    first_face = 1,
    second_face,
    edge_pair,
};

enum class BoxTerrainSatFeature : std::uint8_t {
    terrain_face = 1,
    box_face,
    edge_pair,
};

struct BoxContactPoint final {
    math::Float3 point_on_first{};
    math::Float3 point_on_second{};
    math::Float3 position{};
    float separation{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const BoxContactPoint&,
        const BoxContactPoint&) noexcept = default;
};

struct BoxBoxContactManifold final {
    // Points from the first box toward the second box.
    math::Float3 normal{};
    BoxBoxSatFeature feature{BoxBoxSatFeature::first_face};
    std::uint8_t first_axis_index{};
    std::uint8_t second_axis_index{};
    float separation{};
    float penetration_depth{};
    std::array<BoxContactPoint, box_contact_manifold_capacity> points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const BoxBoxContactManifold&,
        const BoxBoxContactManifold&) noexcept = default;
};

struct BoxTerrainContactPoint final {
    math::Float3 box_point{};
    terrain::HeightTileSurfaceSample surface{};
    math::Float3 position{};
    float separation{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const BoxTerrainContactPoint&,
        const BoxTerrainContactPoint&) noexcept = default;
};

struct BoxTerrainContactManifold final {
    // Non-tangent axes stay in the selected canonical face's upward
    // hemisphere. Near-tangent axes are projected onto the face plane and
    // point from the terrain interval toward the box; their stored float dot
    // with the face normal can differ from zero by representation tolerance.
    math::Float3 normal{};
    BoxTerrainSatFeature feature{BoxTerrainSatFeature::terrain_face};
    std::uint8_t box_axis_index{};
    std::uint8_t triangle_edge_index{};
    float separation{};
    float penetration_depth{};
    std::array<BoxTerrainContactPoint, box_contact_manifold_capacity> points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const BoxTerrainContactManifold&,
        const BoxTerrainContactManifold&) noexcept = default;
};

// Pure geometry queries. Inputs are never mutated, tolerance-touching pairs
// produce a contact, and separated pairs succeed with an empty optional.
[[nodiscard]] core::Result<std::optional<BoxBoxContactManifold>>
query_box_box_contact(
    const RigidBodyState& first_state,
    const BoxCollider& first,
    const RigidBodyState& second_state,
    const BoxCollider& second);

// Tests exact canonical LOD0 triangles as a one-sided upward heightfield and
// returns one deterministic selected-triangle manifold. A box already fully
// below the surface is a discrete-query miss; continuous collision is deferred.
// Rendering meshes and visual LOD never participate.
[[nodiscard]] core::Result<std::optional<BoxTerrainContactManifold>>
query_box_terrain_contact(
    const RigidBodyState& box_state,
    const BoxCollider& box,
    const terrain::HeightTileSurface& terrain_surface);

} // namespace shark::physics
