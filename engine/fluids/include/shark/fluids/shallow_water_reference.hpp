#pragma once

#include <shark/core/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace shark::fluids {

inline constexpr std::uint32_t
    shallow_water_reference_max_columns = 8;
inline constexpr std::uint32_t
    shallow_water_reference_max_rows = 8;
inline constexpr std::size_t
    shallow_water_reference_cell_capacity =
        static_cast<std::size_t>(
            shallow_water_reference_max_columns) *
        shallow_water_reference_max_rows;

struct ShallowWaterGridConfig final {
    std::uint32_t columns{};
    std::uint32_t rows{};

    // World-space X/Z coordinates of the minimum domain corner, in meters.
    double origin_x{};
    double origin_z{};

    // W-002 uses uniform square finite-volume cells, in meters.
    double cell_spacing{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterGridConfig&,
        const ShallowWaterGridConfig&) noexcept = default;
};

struct ShallowWaterConservedState final {
    // Water depth h is in meters. The depth-integrated momenta hu and hv are
    // in square meters per second. They are not kilogram momentum.
    double water_depth{};
    double momentum_x{};
    double momentum_z{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterConservedState&,
        const ShallowWaterConservedState&) noexcept = default;
};

struct ShallowWaterReferenceGrid final {
    ShallowWaterGridConfig config{};

    // Bed elevation b is world-space Y in meters. The active prefix is
    // row-major in +Z, then +X: index = z * columns + x.
    std::array<
        double,
        shallow_water_reference_cell_capacity>
        bed_elevations{};
    std::array<
        ShallowWaterConservedState,
        shallow_water_reference_cell_capacity>
        states{};
    std::size_t cell_count{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterReferenceGrid&,
        const ShallowWaterReferenceGrid&) noexcept = default;
};

struct ShallowWaterCellCenter final {
    double x{};
    double z{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterCellCenter&,
        const ShallowWaterCellCenter&) noexcept = default;
};

struct ShallowWaterCell final {
    double bed_elevation{};
    ShallowWaterConservedState state{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterCell&,
        const ShallowWaterCell&) noexcept = default;
};

enum class ShallowWaterFace : std::uint8_t {
    negative_x = 1,
    positive_x,
    negative_z,
    positive_z,
};

struct ShallowWaterInterface final {
    ShallowWaterCell interior{};
    ShallowWaterCell exterior{};
    bool exterior_is_solid_ghost{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterInterface&,
        const ShallowWaterInterface&) noexcept = default;
};

struct ShallowWaterReferenceDiagnostics final {
    std::size_t cell_count{};
    std::size_t wet_cell_count{};
    std::size_t dry_cell_count{};
    double minimum_water_depth{};
    double maximum_water_depth{};

    // These extrema cover wet cells only. Both are zero when the grid is dry.
    double minimum_free_surface_elevation{};
    double maximum_free_surface_elevation{};

    // Volume is cubic meters. Integrated hu/hv are cubic-meter volume times
    // velocity and therefore have units of m^4/s.
    double water_volume{};
    double integrated_momentum_x{};
    double integrated_momentum_z{};
    double maximum_absolute_momentum_x{};
    double maximum_absolute_momentum_z{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterReferenceDiagnostics&,
        const ShallowWaterReferenceDiagnostics&) noexcept = default;
};

static_assert(std::is_standard_layout_v<ShallowWaterGridConfig>);
static_assert(std::is_trivially_copyable_v<ShallowWaterGridConfig>);
static_assert(
    std::is_standard_layout_v<ShallowWaterConservedState>);
static_assert(
    std::is_trivially_copyable_v<ShallowWaterConservedState>);
static_assert(
    std::is_standard_layout_v<ShallowWaterReferenceGrid>);
static_assert(
    std::is_trivially_copyable_v<ShallowWaterReferenceGrid>);

// Constructs a canonical fixed-capacity record. Inputs are never mutated.
// Exact zero values are normalized to positive zero. A dry cell is
// representable only as h == hu == hv == +0; W-002 does not activate it.
[[nodiscard]] core::Result<ShallowWaterReferenceGrid>
make_shallow_water_reference_grid(
    ShallowWaterGridConfig config,
    std::span<const double> bed_elevations,
    std::span<const ShallowWaterConservedState> states);

// Constructs a fully wet hydrostatic equilibrium with eta = b + h constant
// and exact-zero momentum. Shorelines and wet/dry transitions remain W-004.
[[nodiscard]] core::Result<ShallowWaterReferenceGrid>
make_lake_at_rest_reference_grid(
    ShallowWaterGridConfig config,
    std::span<const double> bed_elevations,
    double free_surface_elevation);

// Revalidates the public record, including its exact active prefix and
// canonical zero tail. This is the finite-state gate for later CPU updates.
[[nodiscard]] core::Result<void>
validate_shallow_water_reference_grid(
    const ShallowWaterReferenceGrid& grid);

[[nodiscard]] core::Result<ShallowWaterCellCenter>
shallow_water_reference_cell_center(
    const ShallowWaterGridConfig& config,
    std::uint32_t column,
    std::uint32_t row);

// Returns the queried interior cell and its cardinal neighbor. At an outer
// edge, the exterior is a reflective solid-wall ghost: bed/depth/tangent
// momentum are copied and only normal momentum is negated.
[[nodiscard]] core::Result<ShallowWaterInterface>
sample_shallow_water_reference_interface(
    const ShallowWaterReferenceGrid& grid,
    std::uint32_t column,
    std::uint32_t row,
    ShallowWaterFace face);

// Revalidates first, then accumulates deterministic row-major baselines for
// later conservation comparisons. No time advancement exists in W-002.
[[nodiscard]] core::Result<ShallowWaterReferenceDiagnostics>
inspect_shallow_water_reference_grid(
    const ShallowWaterReferenceGrid& grid);

} // namespace shark::fluids
