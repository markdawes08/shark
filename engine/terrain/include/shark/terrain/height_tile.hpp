#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace shark::terrain {

struct Bounds3 final {
    math::Float3 minimum;
    math::Float3 maximum;

    [[nodiscard]] friend bool operator==(
        const Bounds3&,
        const Bounds3&) = default;
};

struct HeightTile final {
    std::uint32_t sample_columns{};
    std::uint32_t sample_rows{};
    float sample_spacing{};
    math::Float3 origin;

    // Canonical height offsets are row-major in +Z, then +X:
    // index = z * sample_columns + x. World Y is origin.y + the offset.
    std::vector<float> height_offsets;

    [[nodiscard]] friend bool operator==(
        const HeightTile&,
        const HeightTile&) = default;
};

struct BoundsLineGeometry final {
    std::array<math::Float3, 8> positions;
    std::array<std::uint16_t, 24> indices;

    [[nodiscard]] friend bool operator==(
        const BoundsLineGeometry&,
        const BoundsLineGeometry&) = default;
};

struct HeightTileMesh final {
    std::vector<math::Float3> positions;

    // These area-weighted, smooth normals are presentation data. They are not
    // the exact per-triangle surface normals that T-002 spatial queries use.
    std::vector<math::Float3> normals;
    std::vector<std::uint16_t> indices;
    Bounds3 bounds;
    BoundsLineGeometry bounds_lines;

    [[nodiscard]] friend bool operator==(
        const HeightTileMesh&,
        const HeightTileMesh&) = default;
};

inline constexpr std::uint32_t deterministic_tile_sample_columns = 33;
inline constexpr std::uint32_t deterministic_tile_sample_rows = 33;
inline constexpr float deterministic_tile_sample_spacing = 0.5F;
inline constexpr math::Float3 deterministic_tile_origin{
    -8.0F,
    -2.25F,
    -12.0F,
};
inline constexpr std::size_t deterministic_tile_vertex_count =
    static_cast<std::size_t>(deterministic_tile_sample_columns) *
    static_cast<std::size_t>(deterministic_tile_sample_rows);
inline constexpr std::size_t deterministic_tile_triangle_count =
    static_cast<std::size_t>(
        deterministic_tile_sample_columns - 1U) *
    static_cast<std::size_t>(
        deterministic_tile_sample_rows - 1U) *
    2U;
inline constexpr std::size_t deterministic_tile_index_count =
    deterministic_tile_triangle_count * 3U;
inline constexpr Bounds3 deterministic_tile_expected_bounds{
    {-8.0F, -3.171875F, -12.0F},
    {8.0F, -0.09375F, 4.0F},
};

// Returns Shark's project-owned procedural T-001 fixture. No random state,
// transcendental functions, external content, or platform APIs are involved.
[[nodiscard]] HeightTile make_deterministic_height_tile();

// Builds the exact LOD0 render surface. Every cell uses the diagonal from v00
// to v11 and the +Y winding (v00, v01, v11), (v00, v11, v10), where v10 is
// the next +X sample and v01 is the next +Z sample.
[[nodiscard]] core::Result<HeightTileMesh> build_lod0_mesh(
    const HeightTile& tile);

} // namespace shark::terrain
