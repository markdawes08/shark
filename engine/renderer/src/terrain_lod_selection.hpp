#pragma once

#include <shark/core/math.hpp>

#include <cstdint>
#include <optional>

namespace shark::renderer::detail {

enum class TerrainLod : std::uint8_t {
    lod0 = 1,
    coarse,
};

struct TerrainLodDecision final {
    TerrainLod lod{TerrainLod::lod0};
    double camera_distance{};
};

// A coarse surface is selected when its measured maximum world-space error
// occupies no more than this fraction of the camera-to-bounds distance.
inline constexpr double terrain_coarse_relative_error_limit = 0.008;

// Uses the shortest three-dimensional distance from the camera to the closed
// AABB. Invalid/nonfinite input has no decision. The inclusive threshold also
// deliberately selects an exact (zero-error) coarse surface at zero distance.
[[nodiscard]] std::optional<TerrainLodDecision> select_terrain_lod(
    math::Float3 camera_position,
    math::Float3 bounds_minimum,
    math::Float3 bounds_maximum,
    double maximum_geometric_error) noexcept;

} // namespace shark::renderer::detail
