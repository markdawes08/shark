#pragma once

#include <shark/core/result.hpp>
#include <shark/fluids/shallow_water_reference.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shark::fluids {

inline constexpr double
    standard_gravity_meters_per_second_squared = 9.80665;
inline constexpr double maximum_shallow_water_courant_number = 0.5;
inline constexpr std::uint32_t
    maximum_shallow_water_substep_count = 4'096;

struct ShallowWaterAdvanceSettings final {
    double gravity_meters_per_second_squared{
        standard_gravity_meters_per_second_squared};
    double courant_number{0.45};
    std::uint32_t maximum_substep_count{
        maximum_shallow_water_substep_count};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterAdvanceSettings&,
        const ShallowWaterAdvanceSettings&) noexcept = default;
};

struct ShallowWaterAdvanceReport final {
    std::size_t cell_count{};
    std::uint32_t substep_count{};
    double requested_seconds{};
    double advanced_seconds{};
    double minimum_substep_seconds{};
    double maximum_substep_seconds{};
    double maximum_observed_courant_number{};

    double initial_water_volume{};
    double final_water_volume{};
    double net_outward_boundary_volume{};
    double cumulative_absolute_face_volume{};
    double volume_balance_residual{};
    double maximum_absolute_volume_balance_residual{};
    double volume_balance_tolerance{};

    double final_minimum_water_depth{};
    double final_maximum_absolute_momentum_x{};
    double final_maximum_absolute_momentum_z{};

    [[nodiscard]] friend bool operator==(
        const ShallowWaterAdvanceReport&,
        const ShallowWaterAdvanceReport&) noexcept = default;
};

static_assert(
    std::is_standard_layout_v<ShallowWaterAdvanceSettings>);
static_assert(
    std::is_trivially_copyable_v<ShallowWaterAdvanceSettings>);
static_assert(
    std::is_standard_layout_v<ShallowWaterAdvanceReport>);
static_assert(
    std::is_trivially_copyable_v<ShallowWaterAdvanceReport>);

// Advances one fully wet W-002 reference grid with an unsplit, first-order
// finite-volume update. Rusanov interface fluxes use hydrostatic
// reconstruction and side-specific bed-pressure corrections. Each accepted
// substep is CFL-limited and must remain finite and strictly wet.
//
// The operation is transactional: grid is replaced only after the complete
// requested duration and final diagnostics succeed. Failure never clamps
// depth or velocity and leaves every byte of grid unchanged. The volume
// ledger is enforced with a scale- and operation-aware floating-point
// tolerance reported to the caller. Dry input, reconstructed dry faces, and
// wet/dry activation remain W-004.
[[nodiscard]] core::Result<ShallowWaterAdvanceReport>
advance_shallow_water_reference_grid(
    ShallowWaterReferenceGrid& grid,
    double requested_seconds,
    ShallowWaterAdvanceSettings settings = {});

} // namespace shark::fluids
