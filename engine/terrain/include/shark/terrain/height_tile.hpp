#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

enum class HeightTileTriangle : std::uint8_t {
    v00_v01_v11 = 1,
    v00_v11_v10,
};

struct HeightTileSurfaceSample final {
    math::Float3 position;
    math::Float3 normal;
    std::uint32_t cell_x{};
    std::uint32_t cell_z{};
    HeightTileTriangle triangle{};

    // The components correspond to the vertices named by triangle, in order.
    math::Float3 barycentrics;

    [[nodiscard]] friend bool operator==(
        const HeightTileSurfaceSample&,
        const HeightTileSurfaceSample&) = default;
};

struct Ray3 final {
    math::Float3 origin;
    math::Float3 direction;

    [[nodiscard]] friend bool operator==(
        const Ray3&,
        const Ray3&) = default;
};

struct BoundsInterval final {
    float entry_distance{};
    float exit_distance{};

    [[nodiscard]] friend bool operator==(
        const BoundsInterval&,
        const BoundsInterval&) = default;
};

struct HeightTileRayHit final {
    float distance{};
    math::Float3 position;
    math::Float3 normal;
    std::uint32_t cell_x{};
    std::uint32_t cell_z{};
    HeightTileTriangle triangle{};

    // The components correspond to the vertices named by triangle, in order.
    math::Float3 barycentrics;

    [[nodiscard]] friend bool operator==(
        const HeightTileRayHit&,
        const HeightTileRayHit&) = default;
};

// Owns one validated canonical tile and its cached world-space bounds. Query
// normals are exact geometric LOD0 triangle normals, not smooth render normals.
class HeightTileSurface final {
public:
    [[nodiscard]] static core::Result<HeightTileSurface> create(
        HeightTile tile);

    [[nodiscard]] const HeightTile& tile() const noexcept;
    [[nodiscard]] const Bounds3& bounds() const noexcept;

    // The tile's maximum X/Z edges are inclusive. A point outside the tile or
    // with nonfinite coordinates has no sample.
    [[nodiscard]] std::optional<HeightTileSurfaceSample>
        sample_lod0_surface(float world_x, float world_z) const noexcept;
    [[nodiscard]] std::optional<float> sample_lod0_height(
        float world_x,
        float world_z) const noexcept;
    [[nodiscard]] std::optional<math::Float3> sample_lod0_normal(
        float world_x,
        float world_z) const noexcept;

    // Directions are normalized internally, so all returned distances are
    // meters. Invalid rays or nonfinite/nonpositive limits fail; a valid miss
    // succeeds with an empty optional.
    [[nodiscard]] core::Result<std::optional<BoundsInterval>>
        intersect_bounds(
            const Ray3& ray,
            float maximum_distance) const;
    [[nodiscard]] core::Result<std::optional<HeightTileRayHit>>
        raycast_lod0(
            const Ray3& ray,
            float maximum_distance) const;

private:
    HeightTileSurface(HeightTile tile, Bounds3 bounds);

    HeightTile tile_;
    Bounds3 bounds_;
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

struct HeightTileChunk final {
    std::uint32_t first_cell_x{};
    std::uint32_t first_cell_z{};
    std::uint32_t cell_columns{};
    std::uint32_t cell_rows{};
    std::size_t first_index{};
    std::size_t index_count{};
    Bounds3 bounds;
    BoundsLineGeometry bounds_lines;

    [[nodiscard]] friend bool operator==(
        const HeightTileChunk&,
        const HeightTileChunk&) = default;
};

// References build_lod0_mesh's canonical global vertex stream. Indices are
// concatenated chunk by chunk so each record names one contiguous draw range.
struct HeightTileChunkLayout final {
    std::vector<std::uint16_t> indices;
    std::vector<HeightTileChunk> chunks;

    [[nodiscard]] friend bool operator==(
        const HeightTileChunkLayout&,
        const HeightTileChunkLayout&) = default;
};

struct HeightTileCoarseChunk final {
    std::size_t first_index{};
    std::size_t index_count{};

    // Exact maximum world-space vertical separation, in meters, between this
    // visual surface and the canonical continuous LOD0 surface over the
    // chunk's complete X/Z domain.
    double maximum_geometric_error{};

    [[nodiscard]] friend bool operator==(
        const HeightTileCoarseChunk&,
        const HeightTileCoarseChunk&) = default;
};

// Row-for-row companion to HeightTileChunkLayout. Coarse indices reference
// build_lod0_mesh's unchanged global vertex stream. A partial or odd-sized
// chunk conservatively copies its exact LOD0 range.
struct HeightTileCoarseChunkLayout final {
    std::vector<std::uint16_t> indices;
    std::vector<HeightTileCoarseChunk> chunks;
    double maximum_geometric_error{};

    [[nodiscard]] friend bool operator==(
        const HeightTileCoarseChunkLayout&,
        const HeightTileCoarseChunkLayout&) = default;
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
inline constexpr std::uint32_t deterministic_tile_chunk_cell_columns = 8;
inline constexpr std::uint32_t deterministic_tile_chunk_cell_rows = 8;
inline constexpr std::uint32_t deterministic_tile_chunk_columns = 4;
inline constexpr std::uint32_t deterministic_tile_chunk_rows = 4;
inline constexpr std::size_t deterministic_tile_chunk_count =
    static_cast<std::size_t>(deterministic_tile_chunk_columns) *
    deterministic_tile_chunk_rows;
inline constexpr std::size_t deterministic_tile_chunk_index_count =
    static_cast<std::size_t>(
        deterministic_tile_chunk_cell_columns) *
    deterministic_tile_chunk_cell_rows * 6U;
static_assert(
    deterministic_tile_chunk_count *
        deterministic_tile_chunk_index_count ==
    deterministic_tile_index_count);
inline constexpr std::size_t
    deterministic_tile_coarse_chunk_index_count = 240;
inline constexpr std::size_t deterministic_tile_coarse_index_count =
    deterministic_tile_chunk_count *
    deterministic_tile_coarse_chunk_index_count;
inline constexpr double
    deterministic_tile_coarse_maximum_geometric_error = 0.140625;
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

// Partitions the canonical full-resolution cells into row-major chunks (Z,
// then X). Partial chunks cover the maximum X/Z edges. Every source cell emits
// its two fixed LOD0 triangles exactly once, while each chunk bounds all samples
// referenced by those triangles.
[[nodiscard]] core::Result<HeightTileChunkLayout> build_lod0_chunk_layout(
    const HeightTile& tile,
    std::uint32_t chunk_cell_columns,
    std::uint32_t chunk_cell_rows);

// Derives exactly one boundary-preserving visual LOD for every complete,
// even-sized chunk. Each 2x2-cell patch uses its four corners and canonical
// center; chunk-edge midpoints retain every LOD0 boundary segment. This makes
// all equal- and mixed-LOD seams identical without changing canonical data.
// The returned error is the exact continuous vertical surface-deviation bound,
// measured from all vertices of every coarse/LOD0 triangle intersection.
[[nodiscard]] core::Result<HeightTileCoarseChunkLayout>
    build_boundary_preserving_coarse_chunk_layout(
        const HeightTile& tile,
        std::uint32_t chunk_cell_columns,
        std::uint32_t chunk_cell_rows);

} // namespace shark::terrain
