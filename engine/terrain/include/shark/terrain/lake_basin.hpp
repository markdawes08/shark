#pragma once

#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>

namespace shark::terrain {

struct HorizontalPoint final {
    float x{};
    float z{};

    [[nodiscard]] friend bool operator==(
        const HorizontalPoint&,
        const HorizontalPoint&) = default;
};

// Defines a complete analytic upper bound for the future water surface.
// Points whose normalized squared radius is at most one are inside that
// bounded support. The visible shoreline is where a water surface at the
// published waterline passes the canonical terrain depth test, so bank
// triangles may occlude a narrow part of the support near its edge.
struct LakeBasinFootprint final {
    HorizontalPoint center;
    float semi_axis_x{};
    float semi_axis_z{};

    // The smooth warp is:
    //   x' = dx + (dz^2 - x_warp_square_offset) / x_warp_divisor
    //   z' = dz + (dx^2 - z_warp_square_offset) / z_warp_divisor
    float x_warp_square_offset{};
    float x_warp_divisor{};
    float z_warp_square_offset{};
    float z_warp_divisor{};

    [[nodiscard]] friend bool operator==(
        const LakeBasinFootprint&,
        const LakeBasinFootprint&) = default;
};

struct LakeBasinShape final {
    LakeBasinFootprint footprint;
    float future_waterline_y{};
    float core_depth{};
    float rim_height{};

    // Normalized radii. The terrain rises from the shoreline to the rim,
    // holds the rim through rim_end_radius, then returns to the base terrain
    // at blend_end_radius.
    double rise_end_radius{};
    double rim_end_radius{};
    double blend_end_radius{};

    [[nodiscard]] friend bool operator==(
        const LakeBasinShape&,
        const LakeBasinShape&) = default;
};

[[nodiscard]] double lake_basin_normalized_radius_squared(
    const LakeBasinFootprint& footprint,
    HorizontalPoint point) noexcept;

// Applies a deterministic Q8 height post-process without changing tile
// topology. Inputs outside the safe, exactly Q8-representable envelope are
// rejected. Samples outside blend_end_radius remain byte-identical to base,
// and success guarantees one connected lattice footprint inside a closed
// canonical-triangle rim.
[[nodiscard]] core::Result<HeightTile> shape_closed_lake_basin(
    HeightTile base,
    const LakeBasinShape& shape);

} // namespace shark::terrain
