#include <shark/fluids/shallow_water_reference.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::fluids {
namespace {

[[nodiscard]] core::Error shallow_water_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] constexpr double canonical_zero(
    const double value) noexcept
{
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] bool positive_zero(const double value) noexcept
{
    return value == 0.0 && !std::signbit(value);
}

[[nodiscard]] bool valid_face(
    const ShallowWaterFace face) noexcept
{
    switch (face) {
    case ShallowWaterFace::negative_x:
    case ShallowWaterFace::positive_x:
    case ShallowWaterFace::negative_z:
    case ShallowWaterFace::positive_z:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_config(
    const ShallowWaterGridConfig& config) noexcept
{
    if (config.columns == 0U ||
        config.columns > shallow_water_reference_max_columns ||
        config.rows == 0U ||
        config.rows > shallow_water_reference_max_rows ||
        !std::isfinite(config.origin_x) ||
        !std::isfinite(config.origin_z) ||
        !std::isfinite(config.cell_spacing) ||
        config.cell_spacing <= 0.0) {
        return false;
    }

    const auto cell_area =
        config.cell_spacing * config.cell_spacing;
    if (!std::isfinite(cell_area) || cell_area <= 0.0) {
        return false;
    }

    const auto valid_axis =
        [spacing = config.cell_spacing](
            const double origin,
            const std::uint32_t count) noexcept {
            auto previous_edge = origin;
            for (std::uint32_t index = 0;
                 index < count;
                 ++index) {
                const auto center =
                    origin +
                    (static_cast<double>(index) + 0.5) *
                        spacing;
                const auto next_edge =
                    origin +
                    static_cast<double>(index + 1U) *
                        spacing;
                if (!std::isfinite(center) ||
                    !std::isfinite(next_edge) ||
                    !(center > previous_edge) ||
                    !(next_edge > center)) {
                    return false;
                }
                previous_edge = next_edge;
            }
            return true;
        };
    return valid_axis(config.origin_x, config.columns) &&
        valid_axis(config.origin_z, config.rows);
}

[[nodiscard]] constexpr std::size_t cell_count(
    const ShallowWaterGridConfig& config) noexcept
{
    return static_cast<std::size_t>(config.columns) *
        config.rows;
}

[[nodiscard]] constexpr std::size_t cell_index(
    const ShallowWaterGridConfig& config,
    const std::uint32_t column,
    const std::uint32_t row) noexcept
{
    return static_cast<std::size_t>(row) *
        config.columns + column;
}

[[nodiscard]] bool valid_active_cell(
    const double bed_elevation,
    const ShallowWaterConservedState& state,
    const double cell_area) noexcept
{
    if (!std::isfinite(bed_elevation) ||
        !std::isfinite(state.water_depth) ||
        !std::isfinite(state.momentum_x) ||
        !std::isfinite(state.momentum_z) ||
        (bed_elevation == 0.0 &&
            !positive_zero(bed_elevation)) ||
        (state.water_depth == 0.0 &&
            !positive_zero(state.water_depth)) ||
        (state.momentum_x == 0.0 &&
            !positive_zero(state.momentum_x)) ||
        (state.momentum_z == 0.0 &&
            !positive_zero(state.momentum_z)) ||
        state.water_depth < 0.0) {
        return false;
    }

    if (state.water_depth == 0.0) {
        return positive_zero(state.water_depth) &&
            positive_zero(state.momentum_x) &&
            positive_zero(state.momentum_z);
    }

    const auto free_surface =
        bed_elevation + state.water_depth;
    const auto velocity_x =
        state.momentum_x / state.water_depth;
    const auto velocity_z =
        state.momentum_z / state.water_depth;
    const auto cell_volume =
        state.water_depth * cell_area;
    const auto pressure_scale =
        state.water_depth * state.water_depth;
    const auto integrated_momentum_x =
        state.momentum_x * cell_area;
    const auto integrated_momentum_z =
        state.momentum_z * cell_area;
    const auto advective_flux_x =
        state.momentum_x * velocity_x;
    const auto advective_flux_z =
        state.momentum_z * velocity_z;
    return std::isfinite(free_surface) &&
        std::isfinite(velocity_x) &&
        std::isfinite(velocity_z) &&
        std::isfinite(cell_volume) &&
        std::isfinite(pressure_scale) &&
        std::isfinite(integrated_momentum_x) &&
        std::isfinite(integrated_momentum_z) &&
        std::isfinite(advective_flux_x) &&
        std::isfinite(advective_flux_z);
}

[[nodiscard]] bool canonical_tail_cell(
    const double bed_elevation,
    const ShallowWaterConservedState& state) noexcept
{
    return positive_zero(bed_elevation) &&
        positive_zero(state.water_depth) &&
        positive_zero(state.momentum_x) &&
        positive_zero(state.momentum_z);
}

[[nodiscard]] ShallowWaterCell active_cell(
    const ShallowWaterReferenceGrid& grid,
    const std::uint32_t column,
    const std::uint32_t row) noexcept
{
    const auto index = cell_index(grid.config, column, row);
    return ShallowWaterCell{
        .bed_elevation = grid.bed_elevations[index],
        .state = grid.states[index],
    };
}

[[nodiscard]] double reflected_momentum(
    const double momentum) noexcept
{
    return momentum == 0.0 ? 0.0 : -momentum;
}

class CompensatedSum final {
public:
    [[nodiscard]] bool add(const double value) noexcept
    {
        const auto adjusted = value - correction_;
        const auto next = sum_ + adjusted;
        correction_ = (next - sum_) - adjusted;
        sum_ = next;
        return std::isfinite(sum_) &&
            std::isfinite(correction_);
    }

    [[nodiscard]] double value() const noexcept
    {
        return sum_;
    }

private:
    double sum_{};
    double correction_{};
};

} // namespace

core::Result<ShallowWaterReferenceGrid>
make_shallow_water_reference_grid(
    ShallowWaterGridConfig config,
    const std::span<const double> bed_elevations,
    const std::span<const ShallowWaterConservedState> states)
{
    if (!valid_config(config)) {
        return core::Result<ShallowWaterReferenceGrid>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "A shallow-water reference grid requires 1..8 square "
                "cells per axis, finite origins, and finite positive "
                "spacing with representable extents"));
    }

    const auto expected_count = cell_count(config);
    if (bed_elevations.size() != expected_count ||
        states.size() != expected_count) {
        return core::Result<ShallowWaterReferenceGrid>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "Shallow-water bed and state spans must exactly match "
                "the configured active cell count"));
    }

    const auto cell_area =
        config.cell_spacing * config.cell_spacing;
    for (std::size_t index = 0;
         index < expected_count;
         ++index) {
        auto state = states[index];
        state.water_depth =
            canonical_zero(state.water_depth);
        state.momentum_x =
            canonical_zero(state.momentum_x);
        state.momentum_z =
            canonical_zero(state.momentum_z);
        if (!valid_active_cell(
                canonical_zero(bed_elevations[index]),
                state,
                cell_area)) {
            return core::Result<
                ShallowWaterReferenceGrid>::failure(
                    shallow_water_error(
                        core::ErrorCode::invalid_argument,
                        "Shallow-water cells require finite bed, depth, "
                        "momentum, free surface, velocity, and volume; "
                        "depth must be nonnegative and dry momentum zero"));
        }
    }

    config.origin_x = canonical_zero(config.origin_x);
    config.origin_z = canonical_zero(config.origin_z);
    ShallowWaterReferenceGrid grid{
        .config = config,
        .cell_count = expected_count,
    };
    for (std::size_t index = 0;
         index < expected_count;
         ++index) {
        grid.bed_elevations[index] =
            canonical_zero(bed_elevations[index]);
        grid.states[index] = states[index];
        grid.states[index].water_depth =
            canonical_zero(grid.states[index].water_depth);
        grid.states[index].momentum_x =
            canonical_zero(grid.states[index].momentum_x);
        grid.states[index].momentum_z =
            canonical_zero(grid.states[index].momentum_z);
    }
    return core::Result<ShallowWaterReferenceGrid>::success(
        std::move(grid));
}

core::Result<ShallowWaterReferenceGrid>
make_lake_at_rest_reference_grid(
    const ShallowWaterGridConfig config,
    const std::span<const double> bed_elevations,
    const double free_surface_elevation)
{
    if (!valid_config(config) ||
        bed_elevations.size() != cell_count(config) ||
        !std::isfinite(free_surface_elevation)) {
        return core::Result<ShallowWaterReferenceGrid>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "Lake-at-rest construction requires a valid grid, one "
                "finite bed elevation per cell, and a finite surface"));
    }

    std::array<
        ShallowWaterConservedState,
        shallow_water_reference_cell_capacity>
        states{};
    for (std::size_t index = 0;
         index < bed_elevations.size();
         ++index) {
        const auto bed = bed_elevations[index];
        const auto depth = free_surface_elevation - bed;
        if (!std::isfinite(bed) ||
            !std::isfinite(depth) ||
            depth <= 0.0) {
            return core::Result<
                ShallowWaterReferenceGrid>::failure(
                    shallow_water_error(
                        core::ErrorCode::invalid_argument,
                        "The W-002 lake-at-rest fixture must be fully "
                        "wet with its finite surface above every bed "
                        "cell"));
        }
        states[index].water_depth = depth;
    }

    return make_shallow_water_reference_grid(
        config,
        bed_elevations,
        std::span<const ShallowWaterConservedState>{
            states.data(),
            bed_elevations.size()});
}

core::Result<void> validate_shallow_water_reference_grid(
    const ShallowWaterReferenceGrid& grid)
{
    if (!valid_config(grid.config) ||
        (grid.config.origin_x == 0.0 &&
            !positive_zero(grid.config.origin_x)) ||
        (grid.config.origin_z == 0.0 &&
            !positive_zero(grid.config.origin_z)) ||
        grid.cell_count != cell_count(grid.config)) {
        return core::Result<void>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "Shallow-water grid metadata do not describe the exact "
                "bounded active prefix"));
    }

    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        if (!valid_active_cell(
                grid.bed_elevations[index],
                grid.states[index],
                cell_area)) {
            return core::Result<void>::failure(
                shallow_water_error(
                    core::ErrorCode::invalid_argument,
                    "Shallow-water active state is noncanonical or "
                    "contains an invalid finite-volume cell"));
        }
    }
    for (std::size_t index = grid.cell_count;
         index < shallow_water_reference_cell_capacity;
         ++index) {
        if (!canonical_tail_cell(
                grid.bed_elevations[index],
                grid.states[index])) {
            return core::Result<void>::failure(
                shallow_water_error(
                    core::ErrorCode::invalid_argument,
                    "Shallow-water inactive storage must remain an "
                    "exact positive-zero canonical tail"));
        }
    }

    return core::Result<void>::success();
}

core::Result<ShallowWaterCellCenter>
shallow_water_reference_cell_center(
    const ShallowWaterGridConfig& config,
    const std::uint32_t column,
    const std::uint32_t row)
{
    if (!valid_config(config) ||
        column >= config.columns ||
        row >= config.rows) {
        return core::Result<ShallowWaterCellCenter>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "Shallow-water cell centers require a valid grid and "
                "an in-range row-major coordinate"));
    }

    const auto x =
        config.origin_x +
        (static_cast<double>(column) + 0.5) *
            config.cell_spacing;
    const auto z =
        config.origin_z +
        (static_cast<double>(row) + 0.5) *
            config.cell_spacing;
    if (!std::isfinite(x) || !std::isfinite(z)) {
        return core::Result<ShallowWaterCellCenter>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_state,
                "Shallow-water cell-center calculation overflowed"));
    }
    return core::Result<ShallowWaterCellCenter>::success(
        ShallowWaterCellCenter{.x = x, .z = z});
}

core::Result<ShallowWaterInterface>
sample_shallow_water_reference_interface(
    const ShallowWaterReferenceGrid& grid,
    const std::uint32_t column,
    const std::uint32_t row,
    const ShallowWaterFace face)
{
    const auto validation =
        validate_shallow_water_reference_grid(grid);
    if (!validation) {
        return core::Result<ShallowWaterInterface>::failure(
            core::Error{
                validation.error().category(),
                validation.error().code(),
                std::string{validation.error().message()},
            });
    }
    if (column >= grid.config.columns ||
        row >= grid.config.rows ||
        !valid_face(face)) {
        return core::Result<ShallowWaterInterface>::failure(
            shallow_water_error(
                core::ErrorCode::invalid_argument,
                "Shallow-water interface sampling requires an in-range "
                "cell and one valid cardinal face"));
    }

    auto exterior_column = column;
    auto exterior_row = row;
    auto solid_ghost = false;
    switch (face) {
    case ShallowWaterFace::negative_x:
        if (column == 0U) {
            solid_ghost = true;
        } else {
            --exterior_column;
        }
        break;
    case ShallowWaterFace::positive_x:
        if (column + 1U == grid.config.columns) {
            solid_ghost = true;
        } else {
            ++exterior_column;
        }
        break;
    case ShallowWaterFace::negative_z:
        if (row == 0U) {
            solid_ghost = true;
        } else {
            --exterior_row;
        }
        break;
    case ShallowWaterFace::positive_z:
        if (row + 1U == grid.config.rows) {
            solid_ghost = true;
        } else {
            ++exterior_row;
        }
        break;
    }

    const auto interior =
        active_cell(grid, column, row);
    auto exterior =
        active_cell(grid, exterior_column, exterior_row);
    if (solid_ghost) {
        if (face == ShallowWaterFace::negative_x ||
            face == ShallowWaterFace::positive_x) {
            exterior.state.momentum_x =
                reflected_momentum(
                    exterior.state.momentum_x);
        } else {
            exterior.state.momentum_z =
                reflected_momentum(
                    exterior.state.momentum_z);
        }
    }

    return core::Result<ShallowWaterInterface>::success(
        ShallowWaterInterface{
            .interior = interior,
            .exterior = exterior,
            .exterior_is_solid_ghost = solid_ghost,
        });
}

core::Result<ShallowWaterReferenceDiagnostics>
inspect_shallow_water_reference_grid(
    const ShallowWaterReferenceGrid& grid)
{
    const auto validation =
        validate_shallow_water_reference_grid(grid);
    if (!validation) {
        return core::Result<
            ShallowWaterReferenceDiagnostics>::failure(
                core::Error{
                    validation.error().category(),
                    validation.error().code(),
                    std::string{validation.error().message()},
                });
    }

    ShallowWaterReferenceDiagnostics diagnostics{
        .cell_count = grid.cell_count,
        .minimum_water_depth =
            std::numeric_limits<double>::infinity(),
    };
    auto maximum_depth =
        -std::numeric_limits<double>::infinity();
    auto minimum_surface =
        std::numeric_limits<double>::infinity();
    auto maximum_surface =
        -std::numeric_limits<double>::infinity();
    CompensatedSum volume;
    CompensatedSum momentum_x;
    CompensatedSum momentum_z;
    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;

    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        const auto& state = grid.states[index];
        diagnostics.minimum_water_depth =
            std::min(
                diagnostics.minimum_water_depth,
                state.water_depth);
        maximum_depth =
            std::max(maximum_depth, state.water_depth);
        if (state.water_depth > 0.0) {
            ++diagnostics.wet_cell_count;
            const auto surface =
                grid.bed_elevations[index] +
                state.water_depth;
            minimum_surface =
                std::min(minimum_surface, surface);
            maximum_surface =
                std::max(maximum_surface, surface);
        } else {
            ++diagnostics.dry_cell_count;
        }
        diagnostics.maximum_absolute_momentum_x =
            std::max(
                diagnostics.maximum_absolute_momentum_x,
                std::abs(state.momentum_x));
        diagnostics.maximum_absolute_momentum_z =
            std::max(
                diagnostics.maximum_absolute_momentum_z,
                std::abs(state.momentum_z));

        if (!volume.add(state.water_depth * cell_area) ||
            !momentum_x.add(
                state.momentum_x * cell_area) ||
            !momentum_z.add(
                state.momentum_z * cell_area)) {
            return core::Result<
                ShallowWaterReferenceDiagnostics>::failure(
                    shallow_water_error(
                        core::ErrorCode::invalid_state,
                        "Shallow-water diagnostic accumulation "
                        "overflowed finite double range"));
        }
    }

    diagnostics.maximum_water_depth = maximum_depth;
    if (diagnostics.wet_cell_count != 0U) {
        diagnostics.minimum_free_surface_elevation =
            minimum_surface;
        diagnostics.maximum_free_surface_elevation =
            maximum_surface;
    }
    diagnostics.water_volume = volume.value();
    diagnostics.integrated_momentum_x =
        momentum_x.value();
    diagnostics.integrated_momentum_z =
        momentum_z.value();
    return core::Result<
        ShallowWaterReferenceDiagnostics>::success(
            diagnostics);
}

} // namespace shark::fluids
