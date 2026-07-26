#pragma once

#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>

namespace shark::terrain {

// A deterministic warped ellipse used by both the island terrain profile and
// the presentation-only water shoreline. The polynomial matches the bounded
// water shader:
//   x' = dx + (dz^2 - x_warp_square_offset) / x_warp_divisor
//   z' = dz + (dx^2 - z_warp_square_offset) / z_warp_divisor
struct IslandFootprint final {
    float center_x{};
    float center_z{};
    float semi_axis_x{};
    float semi_axis_z{};
    float x_warp_square_offset{};
    float x_warp_divisor{};
    float z_warp_square_offset{};
    float z_warp_divisor{};

    [[nodiscard]] friend bool operator==(
        const IslandFootprint&,
        const IslandFootprint&) = default;
};

struct IslandShape final {
    IslandFootprint footprint;
    float waterline_y{};
    float shoreline_land_clearance{};
    float interior_land_height{};
    float natural_relief_scale{};
    float shoreline_water_depth{};
    float deep_water_depth{};
    double deep_water_end_radius_squared{};

    [[nodiscard]] friend bool operator==(
        const IslandShape&,
        const IslandShape&) = default;
};

[[nodiscard]] double island_normalized_radius_squared(
    const IslandFootprint& footprint,
    float x,
    float z) noexcept;

// Applies a deterministic Q8 island profile without changing tile topology.
// Success guarantees exactly one connected dry lattice component, no dry
// perimeter samples, a nonempty shallow shelf, and a nonempty deep-water band.
[[nodiscard]] core::Result<HeightTile> shape_playable_island(
    HeightTile base,
    const IslandShape& shape);

} // namespace shark::terrain
