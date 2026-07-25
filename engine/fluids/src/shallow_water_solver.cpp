#include <shark/fluids/shallow_water_solver.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace shark::fluids {
namespace {

inline constexpr std::size_t x_face_capacity =
    static_cast<std::size_t>(
        shallow_water_reference_max_columns + 1U) *
    shallow_water_reference_max_rows;
inline constexpr std::size_t z_face_capacity =
    static_cast<std::size_t>(
        shallow_water_reference_max_rows + 1U) *
    shallow_water_reference_max_columns;

enum class FaceAxis : std::uint8_t {
    x = 1,
    z,
};

struct ConservedFlux final {
    double water_depth{};
    double momentum_x{};
    double momentum_z{};
};

struct SideFlux final {
    ConservedFlux negative_side{};
    ConservedFlux positive_side{};
    double signal_speed{};
};

struct FluxField final {
    std::array<SideFlux, x_face_capacity> x_faces{};
    std::array<SideFlux, z_face_capacity> z_faces{};
};

struct ReconstructedState final {
    ShallowWaterConservedState conserved{};
    double velocity_x{};
    double velocity_z{};
    double pressure{};
    double wave_speed{};
};

struct SubstepResult final {
    ShallowWaterReferenceGrid grid{};
    double discarded_absolute_momentum_x{};
    double discarded_absolute_momentum_z{};
};

struct WetDrySummary final {
    std::size_t active_cell_count{};
    std::size_t retained_film_cell_count{};
    std::size_t exact_dry_cell_count{};
    double retained_film_volume{};
};

struct ProjectedMomentum final {
    double absolute_x{};
    double absolute_z{};
};

[[nodiscard]] core::Error solver_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] core::Error copy_error(
    const core::Error& error)
{
    return core::Error{
        error.category(),
        error.code(),
        std::string{error.message()},
    };
}

[[nodiscard]] constexpr double canonical_zero(
    const double value) noexcept
{
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] constexpr std::size_t cell_index(
    const ShallowWaterGridConfig& config,
    const std::uint32_t column,
    const std::uint32_t row) noexcept
{
    return static_cast<std::size_t>(row) *
        config.columns + column;
}

[[nodiscard]] constexpr std::size_t x_face_index(
    const ShallowWaterGridConfig& config,
    const std::uint32_t face_column,
    const std::uint32_t row) noexcept
{
    return static_cast<std::size_t>(row) *
        (config.columns + 1U) + face_column;
}

[[nodiscard]] constexpr std::size_t z_face_index(
    const ShallowWaterGridConfig& config,
    const std::uint32_t column,
    const std::uint32_t face_row) noexcept
{
    return static_cast<std::size_t>(face_row) *
        config.columns + column;
}

[[nodiscard]] ShallowWaterCell cell_at(
    const ShallowWaterReferenceGrid& grid,
    const std::uint32_t column,
    const std::uint32_t row) noexcept
{
    const auto index =
        cell_index(grid.config, column, row);
    return ShallowWaterCell{
        .bed_elevation = grid.bed_elevations[index],
        .state = grid.states[index],
    };
}

[[nodiscard]] ShallowWaterCell solid_ghost(
    const ShallowWaterCell& cell,
    const FaceAxis axis) noexcept
{
    auto ghost = cell;
    if (axis == FaceAxis::x) {
        ghost.state.momentum_x =
            ghost.state.momentum_x == 0.0
            ? 0.0
            : -ghost.state.momentum_x;
    } else {
        ghost.state.momentum_z =
            ghost.state.momentum_z == 0.0
            ? 0.0
            : -ghost.state.momentum_z;
    }
    return ghost;
}

[[nodiscard]] bool finite_state(
    const ShallowWaterConservedState& state) noexcept
{
    return std::isfinite(state.water_depth) &&
        std::isfinite(state.momentum_x) &&
        std::isfinite(state.momentum_z);
}

[[nodiscard]] bool all_cells_exactly_dry(
    const ShallowWaterReferenceGrid& grid) noexcept
{
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        if (grid.states[index].water_depth != 0.0) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] core::Result<ReconstructedState>
reconstruct_state(
    const ShallowWaterCell& cell,
    const double interface_bed,
    const double gravity,
    const double dry_depth_threshold)
{
    const auto depth = cell.state.water_depth;
    // Subtract beds before adding depth so a small h survives on a very
    // large, equal bed. A negative infinite step is an impassable face.
    const auto bed_step =
        cell.bed_elevation - interface_bed;
    const auto reconstructed_depth =
        std::isinf(bed_step) && bed_step < 0.0
        ? 0.0
        : std::max(0.0, depth + bed_step);
    if (std::isnan(bed_step) ||
        !std::isfinite(reconstructed_depth)) {
        return core::Result<ReconstructedState>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "Hydrostatic reconstruction exceeded finite double "
                "range"));
    }

    const auto velocity_x =
        depth > dry_depth_threshold
        ? cell.state.momentum_x / depth
        : 0.0;
    const auto velocity_z =
        depth > dry_depth_threshold
        ? cell.state.momentum_z / depth
        : 0.0;
    const auto momentum_x =
        reconstructed_depth * velocity_x;
    const auto momentum_z =
        reconstructed_depth * velocity_z;
    const auto gravity_depth =
        gravity * reconstructed_depth;
    const auto pressure =
        0.5 * gravity_depth * reconstructed_depth;
    if (!std::isfinite(velocity_x) ||
        !std::isfinite(velocity_z) ||
        !std::isfinite(momentum_x) ||
        !std::isfinite(momentum_z) ||
        !std::isfinite(gravity_depth) ||
        gravity_depth < 0.0 ||
        !std::isfinite(pressure)) {
        return core::Result<ReconstructedState>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "Hydrostatic reconstruction exceeded finite double "
                "range"));
    }

    const auto wave_speed = std::sqrt(gravity_depth);
    if (!std::isfinite(wave_speed) ||
        wave_speed < 0.0) {
        return core::Result<ReconstructedState>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "Hydrostatic wave speed must be finite and "
                "nonnegative"));
    }
    return core::Result<ReconstructedState>::success(
        ReconstructedState{
            .conserved = {
                .water_depth = reconstructed_depth,
                .momentum_x = momentum_x,
                .momentum_z = momentum_z,
            },
            .velocity_x = velocity_x,
            .velocity_z = velocity_z,
            .pressure = pressure,
            .wave_speed = wave_speed,
        });
}

[[nodiscard]] core::Result<ConservedFlux>
physical_flux(
    const ReconstructedState& state,
    const FaceAxis axis)
{
    ConservedFlux flux;
    if (axis == FaceAxis::x) {
        flux = ConservedFlux{
            .water_depth = state.conserved.momentum_x,
            .momentum_x =
                state.conserved.momentum_x *
                    state.velocity_x +
                state.pressure,
            .momentum_z =
                state.conserved.momentum_x *
                state.velocity_z,
        };
    } else {
        flux = ConservedFlux{
            .water_depth = state.conserved.momentum_z,
            .momentum_x =
                state.conserved.momentum_z *
                state.velocity_x,
            .momentum_z =
                state.conserved.momentum_z *
                    state.velocity_z +
                state.pressure,
        };
    }
    if (!std::isfinite(flux.water_depth) ||
        !std::isfinite(flux.momentum_x) ||
        !std::isfinite(flux.momentum_z)) {
        return core::Result<ConservedFlux>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A physical shallow-water flux exceeded finite "
                "double range"));
    }
    return core::Result<ConservedFlux>::success(flux);
}

[[nodiscard]] core::Result<SideFlux> make_side_flux(
    const ShallowWaterCell& negative_cell,
    const ShallowWaterCell& positive_cell,
    const FaceAxis axis,
    const double gravity,
    const double dry_depth_threshold)
{
    const auto interface_bed =
        std::max(
            negative_cell.bed_elevation,
            positive_cell.bed_elevation);
    const auto negative_result =
        reconstruct_state(
            negative_cell,
            interface_bed,
            gravity,
            dry_depth_threshold);
    if (!negative_result) {
        return core::Result<SideFlux>::failure(
            copy_error(negative_result.error()));
    }
    const auto positive_result =
        reconstruct_state(
            positive_cell,
            interface_bed,
            gravity,
            dry_depth_threshold);
    if (!positive_result) {
        return core::Result<SideFlux>::failure(
            copy_error(positive_result.error()));
    }
    const auto& negative = negative_result.value();
    const auto& positive = positive_result.value();

    const auto negative_flux_result =
        physical_flux(negative, axis);
    if (!negative_flux_result) {
        return core::Result<SideFlux>::failure(
            copy_error(negative_flux_result.error()));
    }
    const auto positive_flux_result =
        physical_flux(positive, axis);
    if (!positive_flux_result) {
        return core::Result<SideFlux>::failure(
            copy_error(positive_flux_result.error()));
    }
    const auto& negative_flux =
        negative_flux_result.value();
    const auto& positive_flux =
        positive_flux_result.value();

    const auto negative_normal_velocity =
        axis == FaceAxis::x
        ? negative.velocity_x
        : negative.velocity_z;
    const auto positive_normal_velocity =
        axis == FaceAxis::x
        ? positive.velocity_x
        : positive.velocity_z;
    const auto signal_speed =
        std::max(
            std::abs(negative_normal_velocity) +
                negative.wave_speed,
            std::abs(positive_normal_velocity) +
                positive.wave_speed);
    if (!std::isfinite(signal_speed) ||
        signal_speed < 0.0) {
        return core::Result<SideFlux>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A Rusanov signal speed must be finite and "
                "nonnegative"));
    }

    const auto rusanov =
        [signal_speed](
            const double negative_flux_value,
            const double positive_flux_value,
            const double negative_state_value,
            const double positive_state_value) noexcept {
            return std::midpoint(
                       negative_flux_value,
                       positive_flux_value) -
                0.5 * signal_speed *
                    (positive_state_value -
                        negative_state_value);
        };
    ConservedFlux base_flux{
        .water_depth = rusanov(
            negative_flux.water_depth,
            positive_flux.water_depth,
            negative.conserved.water_depth,
            positive.conserved.water_depth),
        .momentum_x = rusanov(
            negative_flux.momentum_x,
            positive_flux.momentum_x,
            negative.conserved.momentum_x,
            positive.conserved.momentum_x),
        .momentum_z = rusanov(
            negative_flux.momentum_z,
            positive_flux.momentum_z,
            negative.conserved.momentum_z,
            positive.conserved.momentum_z),
    };
    if (!std::isfinite(base_flux.water_depth) ||
        !std::isfinite(base_flux.momentum_x) ||
        !std::isfinite(base_flux.momentum_z)) {
        return core::Result<SideFlux>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A Rusanov interface flux exceeded finite double "
                "range"));
    }

    const auto original_pressure =
        [gravity](const double depth) noexcept {
            return 0.5 * gravity * depth * depth;
        };
    const auto negative_pressure =
        original_pressure(
            negative_cell.state.water_depth);
    const auto positive_pressure =
        original_pressure(
            positive_cell.state.water_depth);
    if (!std::isfinite(negative_pressure) ||
        !std::isfinite(positive_pressure)) {
        return core::Result<SideFlux>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A bed-pressure correction exceeded finite double "
                "range"));
    }

    auto negative_side = base_flux;
    auto positive_side = base_flux;
    const auto corrected_pressure_flux =
        [](const double base,
           const double reconstructed_pressure,
           const double original_pressure_value) noexcept {
            if (base == reconstructed_pressure) {
                return original_pressure_value;
            }
            return base +
                (original_pressure_value -
                    reconstructed_pressure);
        };
    if (axis == FaceAxis::x) {
        negative_side.momentum_x =
            corrected_pressure_flux(
                base_flux.momentum_x,
                negative.pressure,
                negative_pressure);
        positive_side.momentum_x =
            corrected_pressure_flux(
                base_flux.momentum_x,
                positive.pressure,
                positive_pressure);
    } else {
        negative_side.momentum_z =
            corrected_pressure_flux(
                base_flux.momentum_z,
                negative.pressure,
                negative_pressure);
        positive_side.momentum_z =
            corrected_pressure_flux(
                base_flux.momentum_z,
                positive.pressure,
                positive_pressure);
    }
    if (!std::isfinite(negative_side.momentum_x) ||
        !std::isfinite(negative_side.momentum_z) ||
        !std::isfinite(positive_side.momentum_x) ||
        !std::isfinite(positive_side.momentum_z)) {
        return core::Result<SideFlux>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A corrected interface flux exceeded finite double "
                "range"));
    }

    return core::Result<SideFlux>::success(
        SideFlux{
            .negative_side = negative_side,
            .positive_side = positive_side,
            .signal_speed = signal_speed,
        });
}

[[nodiscard]] core::Result<FluxField> build_flux_field(
    const ShallowWaterReferenceGrid& grid,
    const double gravity,
    const double dry_depth_threshold)
{
    FluxField field;
    for (std::uint32_t row = 0;
         row < grid.config.rows;
         ++row) {
        for (std::uint32_t face_column = 0;
             face_column <= grid.config.columns;
             ++face_column) {
            ShallowWaterCell negative;
            ShallowWaterCell positive;
            if (face_column == 0U) {
                positive = cell_at(grid, 0U, row);
                negative =
                    solid_ghost(positive, FaceAxis::x);
            } else if (
                face_column == grid.config.columns) {
                negative =
                    cell_at(
                        grid,
                        grid.config.columns - 1U,
                        row);
                positive =
                    solid_ghost(negative, FaceAxis::x);
            } else {
                negative =
                    cell_at(grid, face_column - 1U, row);
                positive =
                    cell_at(grid, face_column, row);
            }

            const auto flux_result =
                make_side_flux(
                    negative,
                    positive,
                    FaceAxis::x,
                    gravity,
                    dry_depth_threshold);
            if (!flux_result) {
                return core::Result<FluxField>::failure(
                    copy_error(flux_result.error()));
            }
            field.x_faces[
                x_face_index(
                    grid.config,
                    face_column,
                    row)] = flux_result.value();
        }
    }

    for (std::uint32_t face_row = 0;
         face_row <= grid.config.rows;
         ++face_row) {
        for (std::uint32_t column = 0;
             column < grid.config.columns;
             ++column) {
            ShallowWaterCell negative;
            ShallowWaterCell positive;
            if (face_row == 0U) {
                positive = cell_at(grid, column, 0U);
                negative =
                    solid_ghost(positive, FaceAxis::z);
            } else if (face_row == grid.config.rows) {
                negative =
                    cell_at(
                        grid,
                        column,
                        grid.config.rows - 1U);
                positive =
                    solid_ghost(negative, FaceAxis::z);
            } else {
                negative =
                    cell_at(grid, column, face_row - 1U);
                positive =
                    cell_at(grid, column, face_row);
            }

            const auto flux_result =
                make_side_flux(
                    negative,
                    positive,
                    FaceAxis::z,
                    gravity,
                    dry_depth_threshold);
            if (!flux_result) {
                return core::Result<FluxField>::failure(
                    copy_error(flux_result.error()));
            }
            field.z_faces[
                z_face_index(
                    grid.config,
                    column,
                    face_row)] = flux_result.value();
        }
    }
    return core::Result<FluxField>::success(field);
}

[[nodiscard]] core::Result<double> maximum_cfl_rate(
    const ShallowWaterReferenceGrid& grid,
    const FluxField& field)
{
    auto maximum_rate = 0.0;
    for (std::uint32_t row = 0;
         row < grid.config.rows;
         ++row) {
        for (std::uint32_t column = 0;
             column < grid.config.columns;
             ++column) {
            const auto& west =
                field.x_faces[
                    x_face_index(
                        grid.config,
                        column,
                        row)];
            const auto& east =
                field.x_faces[
                    x_face_index(
                        grid.config,
                        column + 1U,
                        row)];
            const auto& south =
                field.z_faces[
                    z_face_index(
                        grid.config,
                        column,
                        row)];
            const auto& north =
                field.z_faces[
                    z_face_index(
                        grid.config,
                        column,
                        row + 1U)];
            const auto x_rate =
                std::max(
                    west.signal_speed,
                    east.signal_speed) /
                grid.config.cell_spacing;
            const auto z_rate =
                std::max(
                    south.signal_speed,
                    north.signal_speed) /
                grid.config.cell_spacing;
            const auto rate = x_rate + z_rate;
            if (!std::isfinite(rate) || rate < 0.0) {
                return core::Result<double>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "A shallow-water CFL rate must be finite and "
                        "nonnegative"));
            }
            maximum_rate =
                std::max(maximum_rate, rate);
        }
    }
    return core::Result<double>::success(maximum_rate);
}

[[nodiscard]] core::Result<SubstepResult>
apply_substep(
    const ShallowWaterReferenceGrid& grid,
    const FluxField& field,
    const double substep_seconds,
    const double dry_depth_threshold)
{
    auto next = grid;
    auto discarded_absolute_momentum_x = 0.0;
    auto discarded_absolute_momentum_z = 0.0;
    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;
    const auto scale =
        substep_seconds / grid.config.cell_spacing;
    if (!std::isfinite(scale) || scale <= 0.0) {
        return core::Result<SubstepResult>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "A shallow-water substep scale must be finite and "
                "positive"));
    }

    for (std::uint32_t row = 0;
         row < grid.config.rows;
         ++row) {
        for (std::uint32_t column = 0;
             column < grid.config.columns;
             ++column) {
            const auto& west =
                field.x_faces[
                    x_face_index(
                        grid.config,
                        column,
                        row)].positive_side;
            const auto& east =
                field.x_faces[
                    x_face_index(
                        grid.config,
                        column + 1U,
                        row)].negative_side;
            const auto& south =
                field.z_faces[
                    z_face_index(
                        grid.config,
                        column,
                        row)].positive_side;
            const auto& north =
                field.z_faces[
                    z_face_index(
                        grid.config,
                        column,
                        row + 1U)].negative_side;
            const auto index =
                cell_index(grid.config, column, row);
            const auto& current = grid.states[index];

            ShallowWaterConservedState state{
                .water_depth =
                    current.water_depth -
                    scale *
                        ((east.water_depth -
                            west.water_depth) +
                         (north.water_depth -
                            south.water_depth)),
                .momentum_x =
                    current.momentum_x -
                    scale *
                        ((east.momentum_x -
                            west.momentum_x) +
                         (north.momentum_x -
                            south.momentum_x)),
                .momentum_z =
                    current.momentum_z -
                    scale *
                        ((east.momentum_z -
                            west.momentum_z) +
                         (north.momentum_z -
                            south.momentum_z)),
            };
            if (!finite_state(state) ||
                state.water_depth < 0.0) {
                return core::Result<SubstepResult>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "A shallow-water substep became nonfinite or "
                        "negative; no depth clamp was applied"));
            }
            state.water_depth =
                canonical_zero(state.water_depth);
            state.momentum_x =
                canonical_zero(state.momentum_x);
            state.momentum_z =
                canonical_zero(state.momentum_z);
            if (state.water_depth <=
                dry_depth_threshold) {
                const auto discarded_x =
                    std::abs(state.momentum_x) * cell_area;
                const auto discarded_z =
                    std::abs(state.momentum_z) * cell_area;
                if (!std::isfinite(discarded_x) ||
                    !std::isfinite(discarded_z) ||
                    !std::isfinite(
                        discarded_absolute_momentum_x +
                        discarded_x) ||
                    !std::isfinite(
                        discarded_absolute_momentum_z +
                        discarded_z)) {
                    return core::Result<
                        SubstepResult>::failure(
                            solver_error(
                                core::ErrorCode::invalid_state,
                                "Near-dry momentum projection exceeded "
                                "finite double range"));
                }
                discarded_absolute_momentum_x += discarded_x;
                discarded_absolute_momentum_z += discarded_z;
                state.momentum_x = 0.0;
                state.momentum_z = 0.0;
            }
            next.states[index] = state;
        }
    }

    const auto validation =
        validate_shallow_water_reference_grid(next);
    if (!validation) {
        return core::Result<SubstepResult>::failure(
            copy_error(validation.error()));
    }
    return core::Result<SubstepResult>::success(
        SubstepResult{
            .grid = std::move(next),
            .discarded_absolute_momentum_x =
                discarded_absolute_momentum_x,
            .discarded_absolute_momentum_z =
                discarded_absolute_momentum_z,
        });
}

[[nodiscard]] core::Result<double> outward_boundary_rate(
    const ShallowWaterReferenceGrid& grid,
    const FluxField& field)
{
    auto rate = 0.0;
    for (std::uint32_t row = 0;
         row < grid.config.rows;
         ++row) {
        const auto& west =
            field.x_faces[
                x_face_index(grid.config, 0U, row)]
                .positive_side;
        const auto& east =
            field.x_faces[
                x_face_index(
                    grid.config,
                    grid.config.columns,
                    row)]
                .negative_side;
        rate += east.water_depth - west.water_depth;
    }
    for (std::uint32_t column = 0;
         column < grid.config.columns;
         ++column) {
        const auto& south =
            field.z_faces[
                z_face_index(grid.config, column, 0U)]
                .positive_side;
        const auto& north =
            field.z_faces[
                z_face_index(
                    grid.config,
                    column,
                    grid.config.rows)]
                .negative_side;
        rate += north.water_depth - south.water_depth;
    }
    rate *= grid.config.cell_spacing;
    if (!std::isfinite(rate)) {
        return core::Result<double>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "Boundary-volume accounting exceeded finite double "
                "range"));
    }
    return core::Result<double>::success(rate);
}

[[nodiscard]] core::Result<double> absolute_transport_rate(
    const ShallowWaterReferenceGrid& grid,
    const FluxField& field)
{
    auto rate = 0.0;
    for (std::uint32_t row = 0;
         row < grid.config.rows;
         ++row) {
        for (std::uint32_t face_column = 0;
             face_column <= grid.config.columns;
             ++face_column) {
            rate += std::abs(
                field.x_faces[
                    x_face_index(
                        grid.config,
                        face_column,
                        row)]
                    .negative_side.water_depth);
        }
    }
    for (std::uint32_t face_row = 0;
         face_row <= grid.config.rows;
         ++face_row) {
        for (std::uint32_t column = 0;
             column < grid.config.columns;
             ++column) {
            rate += std::abs(
                field.z_faces[
                    z_face_index(
                        grid.config,
                        column,
                        face_row)]
                    .negative_side.water_depth);
        }
    }
    rate *= grid.config.cell_spacing;
    if (!std::isfinite(rate)) {
        return core::Result<double>::failure(
            solver_error(
                core::ErrorCode::invalid_state,
                "Absolute face-volume accounting exceeded finite "
                "double range"));
    }
    return core::Result<double>::success(rate);
}

[[nodiscard]] bool valid_settings(
    const ShallowWaterAdvanceSettings& settings) noexcept
{
    return std::isfinite(
               settings.gravity_meters_per_second_squared) &&
        settings.gravity_meters_per_second_squared > 0.0 &&
        std::isfinite(settings.courant_number) &&
        settings.courant_number > 0.0 &&
        settings.courant_number <=
            maximum_shallow_water_courant_number &&
        std::isfinite(
            settings.dry_depth_threshold_meters) &&
        settings.dry_depth_threshold_meters > 0.0 &&
        settings.maximum_substep_count > 0U &&
        settings.maximum_substep_count <=
            maximum_shallow_water_substep_count;
}

[[nodiscard]] core::Result<WetDrySummary> wet_dry_summary(
    const ShallowWaterReferenceGrid& grid,
    const double dry_depth_threshold)
{
    WetDrySummary summary;
    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        const auto depth =
            grid.states[index].water_depth;
        if (depth == 0.0) {
            ++summary.exact_dry_cell_count;
        } else if (depth > dry_depth_threshold) {
            ++summary.active_cell_count;
        } else {
            ++summary.retained_film_cell_count;
            const auto volume = depth * cell_area;
            if (!std::isfinite(volume) ||
                !std::isfinite(
                    summary.retained_film_volume +
                    volume)) {
                return core::Result<
                    WetDrySummary>::failure(
                        solver_error(
                            core::ErrorCode::invalid_state,
                            "Retained-film diagnostics exceeded finite "
                            "double range"));
            }
            summary.retained_film_volume += volume;
        }
    }
    return core::Result<WetDrySummary>::success(summary);
}

[[nodiscard]] core::Result<ProjectedMomentum>
project_near_dry_momentum(
    ShallowWaterReferenceGrid& grid,
    const double dry_depth_threshold)
{
    ProjectedMomentum projected;
    const auto cell_area =
        grid.config.cell_spacing *
        grid.config.cell_spacing;
    for (std::size_t index = 0;
         index < grid.cell_count;
         ++index) {
        auto& state = grid.states[index];
        if (state.water_depth >
            dry_depth_threshold) {
            continue;
        }
        const auto absolute_x =
            std::abs(state.momentum_x) * cell_area;
        const auto absolute_z =
            std::abs(state.momentum_z) * cell_area;
        if (!std::isfinite(absolute_x) ||
            !std::isfinite(absolute_z) ||
            !std::isfinite(
                projected.absolute_x + absolute_x) ||
            !std::isfinite(
                projected.absolute_z + absolute_z)) {
            return core::Result<ProjectedMomentum>::failure(
                solver_error(
                    core::ErrorCode::invalid_state,
                    "Initial near-dry momentum projection exceeded "
                    "finite double range"));
        }
        projected.absolute_x += absolute_x;
        projected.absolute_z += absolute_z;
        state.momentum_x = 0.0;
        state.momentum_z = 0.0;
    }
    return core::Result<ProjectedMomentum>::success(projected);
}

[[nodiscard]] double volume_balance_tolerance(
    const ShallowWaterReferenceGrid& grid,
    const double initial_volume,
    const double current_volume,
    const double cumulative_absolute_face_volume,
    const std::uint32_t substep_count) noexcept
{
    const auto x_face_count =
        static_cast<std::size_t>(grid.config.columns + 1U) *
        grid.config.rows;
    const auto z_face_count =
        static_cast<std::size_t>(grid.config.rows + 1U) *
        grid.config.columns;
    const auto operation_count =
        static_cast<double>(
            std::max<std::uint32_t>(1U, substep_count)) *
        static_cast<double>(
            grid.cell_count + x_face_count + z_face_count);
    const auto scale =
        std::max({
            std::abs(initial_volume),
            std::abs(current_volume),
            cumulative_absolute_face_volume,
        });
    return std::max(
        std::numeric_limits<double>::denorm_min(),
        128.0 *
            std::numeric_limits<double>::epsilon() *
            operation_count * scale);
}

} // namespace

core::Result<ShallowWaterAdvanceReport>
advance_shallow_water_reference_grid(
    ShallowWaterReferenceGrid& grid,
    const double requested_seconds,
    const ShallowWaterAdvanceSettings settings)
{
    const auto validation =
        validate_shallow_water_reference_grid(grid);
    if (!validation) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(validation.error()));
    }
    if (!std::isfinite(requested_seconds) ||
        requested_seconds <= 0.0 ||
        !valid_settings(settings)) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                solver_error(
                    core::ErrorCode::invalid_argument,
                    "A shallow-water advance requires positive finite "
                    "time, gravity, and dry-depth threshold; "
                    "0 < CFL <= 0.5; and a nonzero substep budget no "
                    "greater than 4096"));
    }

    const auto initial_diagnostics =
        inspect_shallow_water_reference_grid(grid);
    if (!initial_diagnostics) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(initial_diagnostics.error()));
    }
    const auto initial_wet_dry =
        wet_dry_summary(
            grid,
            settings.dry_depth_threshold_meters);
    if (!initial_wet_dry) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(initial_wet_dry.error()));
    }

    auto candidate = grid;
    const auto initial_projection =
        project_near_dry_momentum(
            candidate,
            settings.dry_depth_threshold_meters);
    if (!initial_projection) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(initial_projection.error()));
    }
    auto advanced_seconds = 0.0;
    auto net_outward_volume = 0.0;
    auto cumulative_absolute_face_volume = 0.0;
    ShallowWaterAdvanceReport report{
        .cell_count = grid.cell_count,
        .requested_seconds = requested_seconds,
        .minimum_substep_seconds =
            std::numeric_limits<double>::infinity(),
        .dry_depth_threshold_meters =
            settings.dry_depth_threshold_meters,
        .initial_active_cell_count =
            initial_wet_dry.value().active_cell_count,
        .initial_retained_film_cell_count =
            initial_wet_dry.value().
                retained_film_cell_count,
        .initial_exact_dry_cell_count =
            initial_wet_dry.value().exact_dry_cell_count,
        .cumulative_discarded_absolute_momentum_x =
            initial_projection.value().absolute_x,
        .cumulative_discarded_absolute_momentum_z =
            initial_projection.value().absolute_z,
        .initial_water_volume =
            initial_diagnostics.value().water_volume,
    };

    while (advanced_seconds < requested_seconds) {
        if (report.substep_count >=
            settings.maximum_substep_count) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::unavailable,
                        "The shallow-water advance exhausted its "
                        "configured CFL substep budget"));
        }

        const auto field_result =
            build_flux_field(
                candidate,
                settings.gravity_meters_per_second_squared,
                settings.dry_depth_threshold_meters);
        if (!field_result) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(field_result.error()));
        }
        const auto& field = field_result.value();
        const auto rate_result =
            maximum_cfl_rate(candidate, field);
        if (!rate_result) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(rate_result.error()));
        }
        const auto cfl_rate = rate_result.value();
        const auto remaining_seconds =
            requested_seconds - advanced_seconds;
        const auto remaining_courant =
            remaining_seconds * cfl_rate;
        const auto final_substep =
            std::isfinite(remaining_courant) &&
            remaining_courant <= settings.courant_number;
        auto substep_seconds =
            final_substep
            ? remaining_seconds
            : settings.courant_number / cfl_rate;
        if (!std::isfinite(substep_seconds) ||
            substep_seconds <= 0.0) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "The shallow-water CFL timestep is not finite "
                        "and positive"));
        }

        if (final_substep) {
            substep_seconds = remaining_seconds;
        } else if (
            advanced_seconds + substep_seconds <=
                advanced_seconds) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::unavailable,
                        "The shallow-water CFL substep cannot make "
                        "representable time progress"));
        }

        // An exactly dry candidate has zero projected momentum and zero
        // face flux. Preserve it directly: dt / dx can otherwise overflow
        // or underflow even though there is no update to perform. A rounded
        // zero CFL rate alone is not sufficient because signal / dx can
        // underflow while a wet face still transports water.
        const auto exact_dry_no_op =
            cfl_rate == 0.0 &&
            all_cells_exactly_dry(candidate);
        const auto next_result =
            exact_dry_no_op
            ? core::Result<SubstepResult>::success(
                SubstepResult{.grid = candidate})
            : apply_substep(
                candidate,
                field,
                substep_seconds,
                settings.dry_depth_threshold_meters);
        if (!next_result) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(next_result.error()));
        }
        const auto boundary_rate_result =
            outward_boundary_rate(candidate, field);
        if (!boundary_rate_result) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(boundary_rate_result.error()));
        }
        const auto outward_volume =
            boundary_rate_result.value() *
            substep_seconds;
        const auto transport_rate_result =
            absolute_transport_rate(candidate, field);
        if (!transport_rate_result) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(transport_rate_result.error()));
        }
        const auto absolute_face_volume =
            transport_rate_result.value() *
            substep_seconds;
        if (!std::isfinite(outward_volume) ||
            !std::isfinite(absolute_face_volume) ||
            !std::isfinite(
                net_outward_volume + outward_volume) ||
            !std::isfinite(
                cumulative_absolute_face_volume +
                    absolute_face_volume) ||
            !std::isfinite(
                report.
                    cumulative_discarded_absolute_momentum_x +
                next_result.value().
                    discarded_absolute_momentum_x) ||
            !std::isfinite(
                report.
                    cumulative_discarded_absolute_momentum_z +
                next_result.value().
                    discarded_absolute_momentum_z)) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "The shallow-water boundary ledger exceeded "
                        "finite double range"));
        }

        for (std::size_t index = 0;
             index < candidate.cell_count;
             ++index) {
            const auto was_active =
                candidate.states[index].water_depth >
                settings.dry_depth_threshold_meters;
            const auto is_active =
                next_result.value().
                    grid.states[index].water_depth >
                settings.dry_depth_threshold_meters;
            if (!was_active && is_active) {
                ++report.activation_count;
            } else if (was_active && !is_active) {
                ++report.deactivation_count;
            }
        }
        candidate = next_result.value().grid;
        net_outward_volume += outward_volume;
        cumulative_absolute_face_volume +=
            absolute_face_volume;
        report.cumulative_discarded_absolute_momentum_x +=
            next_result.value().
                discarded_absolute_momentum_x;
        report.cumulative_discarded_absolute_momentum_z +=
            next_result.value().
                discarded_absolute_momentum_z;
        advanced_seconds =
            final_substep
            ? requested_seconds
            : advanced_seconds + substep_seconds;
        ++report.substep_count;
        report.minimum_substep_seconds =
            std::min(
                report.minimum_substep_seconds,
                substep_seconds);
        report.maximum_substep_seconds =
            std::max(
                report.maximum_substep_seconds,
                substep_seconds);
        report.maximum_observed_courant_number =
            std::max(
                report.maximum_observed_courant_number,
                substep_seconds * cfl_rate);

        const auto diagnostics =
            inspect_shallow_water_reference_grid(candidate);
        if (!diagnostics) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    copy_error(diagnostics.error()));
        }
        const auto balance =
            diagnostics.value().water_volume -
            report.initial_water_volume +
            net_outward_volume;
        if (!std::isfinite(balance)) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "The shallow-water volume ledger exceeded "
                        "finite double range"));
        }
        report.volume_balance_tolerance =
            volume_balance_tolerance(
                candidate,
                report.initial_water_volume,
                diagnostics.value().water_volume,
                cumulative_absolute_face_volume,
                report.substep_count);
        if (!std::isfinite(
                report.volume_balance_tolerance) ||
            std::abs(balance) >
                report.volume_balance_tolerance) {
            return core::Result<
                ShallowWaterAdvanceReport>::failure(
                    solver_error(
                        core::ErrorCode::invalid_state,
                        "The shallow-water volume ledger exceeded its "
                        "scale-aware floating-point tolerance"));
        }
        report.maximum_absolute_volume_balance_residual =
            std::max(
                report.
                    maximum_absolute_volume_balance_residual,
                std::abs(balance));
    }

    const auto final_diagnostics =
        inspect_shallow_water_reference_grid(candidate);
    if (!final_diagnostics) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(final_diagnostics.error()));
    }
    const auto final_wet_dry =
        wet_dry_summary(
            candidate,
            settings.dry_depth_threshold_meters);
    if (!final_wet_dry) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                copy_error(final_wet_dry.error()));
    }
    report.advanced_seconds = requested_seconds;
    report.final_active_cell_count =
        final_wet_dry.value().active_cell_count;
    report.final_retained_film_cell_count =
        final_wet_dry.value().retained_film_cell_count;
    report.final_exact_dry_cell_count =
        final_wet_dry.value().exact_dry_cell_count;
    report.final_retained_film_volume =
        final_wet_dry.value().retained_film_volume;
    report.final_water_volume =
        final_diagnostics.value().water_volume;
    report.net_outward_boundary_volume =
        net_outward_volume;
    report.cumulative_absolute_face_volume =
        cumulative_absolute_face_volume;
    report.volume_balance_residual =
        report.final_water_volume -
        report.initial_water_volume +
        net_outward_volume;
    report.final_minimum_water_depth =
        final_diagnostics.value().minimum_water_depth;
    report.final_maximum_absolute_momentum_x =
        final_diagnostics.value().
            maximum_absolute_momentum_x;
    report.final_maximum_absolute_momentum_z =
        final_diagnostics.value().
            maximum_absolute_momentum_z;
    if (!std::isfinite(report.volume_balance_residual) ||
        report.final_minimum_water_depth < 0.0 ||
        report.maximum_absolute_volume_balance_residual >
            report.volume_balance_tolerance) {
        return core::Result<
            ShallowWaterAdvanceReport>::failure(
                solver_error(
                    core::ErrorCode::invalid_state,
                    "The final shallow-water state or volume ledger "
                    "is invalid"));
    }

    grid = candidate;
    return core::Result<ShallowWaterAdvanceReport>::success(
        report);
}

} // namespace shark::fluids
