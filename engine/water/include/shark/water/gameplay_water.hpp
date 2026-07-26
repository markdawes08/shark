#pragma once

#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/island.hpp>

#include <cstdint>
#include <optional>

namespace shark::water {

// The current island terrain is Q8. Treating one height quantum as a dry
// shoreline band prevents tiny triangle-interpolation differences from
// repeatedly changing gameplay containment at the visible waterline.
inline constexpr float default_shoreline_depth_tolerance =
    1.0F / 256.0F;

enum class CalmWaterSupportSide : std::uint8_t {
    inside_warped_footprint = 1,
    outside_warped_footprint,
};

struct HorizontalFlow final {
    float x{};
    float z{};

    [[nodiscard]] friend bool operator==(
        const HorizontalFlow&,
        const HorizontalFlow&) noexcept = default;
};

// Authored equilibrium-water data. This record contains no renderer settings,
// character thresholds, simulated-fluid state, or GPU resources.
struct CalmWaterBody final {
    terrain::IslandFootprint footprint;
    CalmWaterSupportSide support_side{
        CalmWaterSupportSide::inside_warped_footprint};
    float surface_height{};
    float shoreline_depth_tolerance{
        default_shoreline_depth_tolerance};
    std::optional<HorizontalFlow> flow_velocity;

    [[nodiscard]] friend bool operator==(
        const CalmWaterBody&,
        const CalmWaterBody&) noexcept = default;
};

enum class GameplayWaterDisposition : std::uint8_t {
    out_of_terrain = 1,
    no_water,
    water,
};

struct GameplayWaterQuery final {
    GameplayWaterDisposition disposition{
        GameplayWaterDisposition::out_of_terrain};
    bool horizontal_support{};
    float surface_height{};
    float bed_height{};
    float depth{};

    // Present only when disposition is water and the body authors flow.
    // Calm water may explicitly publish a present positive-zero flow.
    std::optional<HorizontalFlow> flow_velocity;

    [[nodiscard]] friend bool operator==(
        const GameplayWaterQuery&,
        const GameplayWaterQuery&) noexcept = default;
};

// Queries one vertical water column over the exact canonical LOD0 terrain
// surface. Invalid body/coordinate input fails. A finite point beyond the
// terrain is a successful out_of_terrain result and does not evaluate warped
// support. Water requires inclusive authored horizontal support and depth
// strictly greater than the body's shoreline tolerance; inactive depth/flow
// fields are positive zero/empty.
[[nodiscard]] core::Result<GameplayWaterQuery> query_gameplay_water(
    const CalmWaterBody& body,
    const terrain::HeightTileSurface& terrain_surface,
    float world_x,
    float world_z);

} // namespace shark::water
