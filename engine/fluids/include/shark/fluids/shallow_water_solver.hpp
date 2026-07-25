#pragma once

#include <shark/core/result.hpp>
#include <shark/fluids/shallow_water_reference.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shark::fluids {

inline constexpr double
    standard_gravity_meters_per_second_squared = 9.80665;
inline constexpr double
    default_shallow_water_dry_depth_meters = 0x1.0p-20;
inline constexpr double maximum_shallow_water_courant_number = 0.5;
inline constexpr std::uint32_t
    maximum_shallow_water_substep_count = 4'096;

struct ShallowWaterAdvanceSettings final {
    double gravity_meters_per_second_squared{
        standard_gravity_meters_per_second_squared};
    double courant_number{0.45};
    double dry_depth_threshold_meters{
        default_shallow_water_dry_depth_meters};
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
    double dry_depth_threshold_meters{};

    std::size_t initial_active_cell_count{};
    std::size_t initial_retained_film_cell_count{};
    std::size_t initial_exact_dry_cell_count{};
    std::size_t final_active_cell_count{};
    std::size_t final_retained_film_cell_count{};
    std::size_t final_exact_dry_cell_count{};

    // Accepted-substep threshold-crossing events. One cell can contribute
    // repeatedly. Exact-dry <-> retained-film changes are not transitions;
    // activation is h <= threshold -> h > threshold and deactivation is
    // the reverse.
    std::uint32_t activation_count{};
    std::uint32_t deactivation_count{};

    // Retained film is cubic meters. Discarded momentum is the cumulative
    // absolute depth-integrated projection per axis, in m^4/s.
    double final_retained_film_volume{};
    double cumulative_discarded_absolute_momentum_x{};
    double cumulative_discarded_absolute_momentum_z{};

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

// Advances one W-002 reference grid with an unsplit, first-order finite-volume
// update. Rusanov interface fluxes use hydrostatic reconstruction and
// side-specific bed-pressure corrections. Each accepted substep is
// CFL-limited, finite, and nonnegative.
//
// The operation is transactional: grid is replaced only after the complete
// requested duration and final diagnostics succeed. Failure never clamps
// depth or velocity and leaves every byte of grid unchanged. The volume
// ledger is enforced with a scale- and operation-aware floating-point
// tolerance reported to the caller.
//
// A cell is numerically active only when h > dry_depth_threshold_meters.
// Shallower positive cells retain their depth as a conservative film while
// their velocity and momentum become exact zero. Their retained depth still
// participates in hydrostatic reconstruction and pressure. Shared face fluxes
// can raise the film above the threshold and reactivate it. The report makes
// discarded near-dry momentum explicit; the policy never discards or
// redistributes water merely to classify a cell as numerically dry.
[[nodiscard]] core::Result<ShallowWaterAdvanceReport>
advance_shallow_water_reference_grid(
    ShallowWaterReferenceGrid& grid,
    double requested_seconds,
    ShallowWaterAdvanceSettings settings = {});

} // namespace shark::fluids
