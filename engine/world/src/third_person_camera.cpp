#include <shark/world/third_person_camera.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::world {
namespace {

[[nodiscard]] core::Error camera_rig_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] float canonical_zero(const float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] math::Float3 canonical_zero(
    const math::Float3 value) noexcept
{
    return {
        canonical_zero(value.x),
        canonical_zero(value.y),
        canonical_zero(value.z),
    };
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] float wrap_yaw(const double yaw) noexcept
{
    auto wrapped = std::remainder(
        yaw,
        static_cast<double>(math::two_pi));
    if (wrapped >= static_cast<double>(math::pi)) {
        wrapped -= static_cast<double>(math::two_pi);
    }
    auto result = static_cast<float>(wrapped);
    if (result >= math::pi) {
        result -= math::two_pi;
    }
    return canonical_zero(result);
}

[[nodiscard]] bool canonical_yaw(const float yaw) noexcept
{
    return std::isfinite(yaw) &&
        yaw >= -math::pi &&
        yaw < math::pi &&
        (yaw != 0.0F || !std::signbit(yaw));
}

[[nodiscard]] bool valid_lens(
    const PerspectiveLens& lens) noexcept
{
    return std::isfinite(lens.vertical_fov_radians) &&
        std::isfinite(lens.near_plane) &&
        std::isfinite(lens.far_plane) &&
        lens.vertical_fov_radians > 0.0F &&
        lens.vertical_fov_radians < math::pi &&
        lens.near_plane > 0.0F &&
        lens.far_plane > lens.near_plane;
}

[[nodiscard]] bool valid_orbit(
    const ThirdPersonCameraConfig& config,
    const ThirdPersonOrbitState& orbit) noexcept
{
    return canonical_yaw(orbit.yaw_radians) &&
        std::isfinite(orbit.pitch_radians) &&
        orbit.pitch_radians >= config.minimum_pitch_radians &&
        orbit.pitch_radians <= config.maximum_pitch_radians &&
        std::isfinite(orbit.boom_distance) &&
        orbit.boom_distance >= config.minimum_boom_distance &&
        orbit.boom_distance <= config.maximum_boom_distance;
}

[[nodiscard]] bool neutral_delta(
    const ThirdPersonOrbitDelta& delta) noexcept
{
    return delta == ThirdPersonOrbitDelta{};
}

[[nodiscard]] core::Result<float> interpolate_component(
    const float previous,
    const float current,
    const double alpha)
{
    const auto value =
        static_cast<double>(previous) +
        (static_cast<double>(current) - previous) * alpha;
    if (!representable_float(value)) {
        return core::Result<float>::failure(camera_rig_error(
            core::ErrorCode::unavailable,
            "Third-person camera interpolation exceeded finite "
            "float range"));
    }
    return core::Result<float>::success(
        canonical_zero(static_cast<float>(value)));
}

[[nodiscard]] core::Result<math::Float3> orbit_forward(
    const ThirdPersonOrbitState& orbit)
{
    const auto cosine_pitch =
        std::cos(static_cast<double>(orbit.pitch_radians));
    const auto forward_x =
        std::sin(static_cast<double>(orbit.yaw_radians)) *
        cosine_pitch;
    const auto forward_y =
        std::sin(static_cast<double>(orbit.pitch_radians));
    const auto forward_z =
        -std::cos(static_cast<double>(orbit.yaw_radians)) *
        cosine_pitch;
    if (!representable_float(forward_x) ||
        !representable_float(forward_y) ||
        !representable_float(forward_z)) {
        return core::Result<math::Float3>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person orbit direction exceeded finite "
                "float range"));
    }
    return core::Result<math::Float3>::success({
        canonical_zero(static_cast<float>(forward_x)),
        canonical_zero(static_cast<float>(forward_y)),
        canonical_zero(static_cast<float>(forward_z)),
    });
}

[[nodiscard]] core::Result<math::Float3> point_on_boom(
    const math::Float3 target,
    const math::Float3 forward,
    const double distance)
{
    const auto x =
        static_cast<double>(target.x) -
        static_cast<double>(forward.x) * distance;
    const auto y =
        static_cast<double>(target.y) -
        static_cast<double>(forward.y) * distance;
    const auto z =
        static_cast<double>(target.z) -
        static_cast<double>(forward.z) * distance;
    if (!representable_float(x) ||
        !representable_float(y) ||
        !representable_float(z)) {
        return core::Result<math::Float3>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera boom exceeded finite world "
                "coordinates"));
    }
    return core::Result<math::Float3>::success(canonical_zero({
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z),
    }));
}

} // namespace

bool is_valid(
    const ThirdPersonCameraConfig& config) noexcept
{
    return std::isfinite(config.target_height_offset) &&
        config.target_height_offset >= 0.0F &&
        config.target_height_offset <=
            maximum_third_person_target_height_offset &&
        std::isfinite(config.minimum_pitch_radians) &&
        std::isfinite(config.maximum_pitch_radians) &&
        config.minimum_pitch_radians >=
            -maximum_camera_pitch &&
        config.maximum_pitch_radians <=
            maximum_camera_pitch &&
        config.minimum_pitch_radians <=
            config.maximum_pitch_radians &&
        canonical_yaw(config.initial_yaw_radians) &&
        std::isfinite(config.initial_pitch_radians) &&
        config.initial_pitch_radians >=
            config.minimum_pitch_radians &&
        config.initial_pitch_radians <=
            config.maximum_pitch_radians &&
        std::isfinite(config.minimum_boom_distance) &&
        config.minimum_boom_distance > 0.0F &&
        std::isfinite(config.maximum_boom_distance) &&
        config.maximum_boom_distance >=
            config.minimum_boom_distance &&
        config.maximum_boom_distance <=
            maximum_third_person_boom_distance &&
        std::isfinite(config.initial_boom_distance) &&
        config.initial_boom_distance >=
            config.minimum_boom_distance &&
        config.initial_boom_distance <=
            config.maximum_boom_distance &&
        std::isfinite(config.obstruction_clearance) &&
        config.obstruction_clearance > 0.0F &&
        config.obstruction_clearance <=
            maximum_third_person_obstruction_clearance;
}

bool is_valid(
    const ThirdPersonOrbitDelta& delta) noexcept
{
    return std::isfinite(delta.yaw_radians) &&
        std::isfinite(delta.pitch_radians) &&
        std::isfinite(delta.boom_distance);
}

bool is_valid(
    const ThirdPersonCameraRig& rig) noexcept
{
    if (!is_valid(rig.config) ||
        !valid_orbit(rig.config, rig.previous.state) ||
        !valid_orbit(rig.config, rig.current.state) ||
        !is_valid(rig.previous.consumed_delta) ||
        !is_valid(rig.current.consumed_delta)) {
        return false;
    }
    if (rig.current.fixed_tick == 0U) {
        const ThirdPersonOrbitState initial{
            .yaw_radians = rig.config.initial_yaw_radians,
            .pitch_radians = rig.config.initial_pitch_radians,
            .boom_distance = rig.config.initial_boom_distance,
        };
        return rig.previous.fixed_tick == 0U &&
            rig.previous.state == initial &&
            rig.current.state == initial &&
            neutral_delta(rig.previous.consumed_delta) &&
            neutral_delta(rig.current.consumed_delta);
    }
    return rig.previous.fixed_tick !=
            std::numeric_limits<std::uint64_t>::max() &&
        rig.previous.fixed_tick + 1U ==
            rig.current.fixed_tick;
}

core::Result<ThirdPersonCameraRig>
create_third_person_camera_rig(ThirdPersonCameraConfig config)
{
    if (!std::isfinite(config.initial_yaw_radians)) {
        return core::Result<ThirdPersonCameraRig>::failure(
            camera_rig_error(
                core::ErrorCode::invalid_argument,
                "Third-person camera requires a finite initial yaw"));
    }

    config.target_height_offset =
        canonical_zero(config.target_height_offset);
    config.minimum_pitch_radians =
        canonical_zero(config.minimum_pitch_radians);
    config.maximum_pitch_radians =
        canonical_zero(config.maximum_pitch_radians);
    config.initial_yaw_radians = wrap_yaw(
        static_cast<double>(config.initial_yaw_radians));
    config.initial_pitch_radians =
        canonical_zero(config.initial_pitch_radians);
    config.minimum_boom_distance =
        canonical_zero(config.minimum_boom_distance);
    config.maximum_boom_distance =
        canonical_zero(config.maximum_boom_distance);
    config.initial_boom_distance =
        canonical_zero(config.initial_boom_distance);
    config.obstruction_clearance =
        canonical_zero(config.obstruction_clearance);
    if (!is_valid(config)) {
        return core::Result<ThirdPersonCameraRig>::failure(
            camera_rig_error(
                core::ErrorCode::invalid_argument,
                "Third-person camera requires bounded pitch, boom, "
                "target-offset, and obstruction-clearance values"));
    }

    const ThirdPersonOrbitSnapshot initial{
        .state = {
            .yaw_radians = config.initial_yaw_radians,
            .pitch_radians = config.initial_pitch_radians,
            .boom_distance = config.initial_boom_distance,
        },
    };
    ThirdPersonCameraRig rig{
        .config = config,
        .previous = initial,
        .current = initial,
    };
    if (!is_valid(rig)) {
        return core::Result<ThirdPersonCameraRig>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera could not publish canonical "
                "initial snapshots"));
    }
    return core::Result<ThirdPersonCameraRig>::success(rig);
}

core::Result<void> advance_third_person_camera_rig(
    ThirdPersonCameraRig& rig,
    ThirdPersonOrbitDelta delta,
    const std::uint64_t fixed_tick)
{
    if (!is_valid(rig) || !is_valid(delta)) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::invalid_argument,
            "Third-person camera advance requires valid authoritative "
            "state and finite orbit deltas"));
    }
    if (rig.current.fixed_tick ==
        std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::unavailable,
            "Third-person camera fixed-tick counter would overflow"));
    }
    if (fixed_tick != rig.current.fixed_tick + 1U) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::invalid_argument,
            "Third-person camera fixed ticks must be consecutive"));
    }

    delta.yaw_radians = canonical_zero(delta.yaw_radians);
    delta.pitch_radians = canonical_zero(delta.pitch_radians);
    delta.boom_distance = canonical_zero(delta.boom_distance);

    const auto pitch = std::clamp(
        static_cast<double>(rig.current.state.pitch_radians) +
            static_cast<double>(delta.pitch_radians),
        static_cast<double>(rig.config.minimum_pitch_radians),
        static_cast<double>(rig.config.maximum_pitch_radians));
    const auto distance = std::clamp(
        static_cast<double>(rig.current.state.boom_distance) +
            static_cast<double>(delta.boom_distance),
        static_cast<double>(rig.config.minimum_boom_distance),
        static_cast<double>(rig.config.maximum_boom_distance));

    auto candidate = rig;
    candidate.previous = rig.current;
    candidate.current = ThirdPersonOrbitSnapshot{
        .state = {
            .yaw_radians = wrap_yaw(
                static_cast<double>(
                    rig.current.state.yaw_radians) +
                static_cast<double>(delta.yaw_radians)),
            .pitch_radians = canonical_zero(
                static_cast<float>(pitch)),
            .boom_distance = canonical_zero(
                static_cast<float>(distance)),
        },
        .consumed_delta = delta,
        .fixed_tick = fixed_tick,
    };
    if (!is_valid(candidate)) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::unavailable,
            "Third-person camera advance could not publish valid "
            "ordered snapshots"));
    }
    rig = candidate;
    return core::Result<void>::success();
}

core::Result<void>
collapse_third_person_camera_interpolation(
    ThirdPersonCameraRig& rig)
{
    if (!is_valid(rig)) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::invalid_argument,
            "Third-person camera interpolation collapse requires "
            "valid authoritative state"));
    }

    auto candidate = rig;
    candidate.previous.state = candidate.current.state;
    if (!is_valid(candidate)) {
        return core::Result<void>::failure(camera_rig_error(
            core::ErrorCode::unavailable,
            "Third-person camera could not collapse its interpolation "
            "interval"));
    }
    rig = candidate;
    return core::Result<void>::success();
}

core::Result<ThirdPersonOrbitState>
interpolate_third_person_camera_rig(
    const ThirdPersonCameraRig& rig,
    const float alpha)
{
    if (!is_valid(rig) ||
        !std::isfinite(alpha) ||
        alpha < 0.0F ||
        alpha > 1.0F) {
        return core::Result<ThirdPersonOrbitState>::failure(
            camera_rig_error(
                core::ErrorCode::invalid_argument,
                "Third-person camera interpolation requires valid "
                "snapshots and alpha in [0, 1]"));
    }
    if (alpha == 0.0F) {
        return core::Result<ThirdPersonOrbitState>::success(
            rig.previous.state);
    }
    if (alpha == 1.0F) {
        return core::Result<ThirdPersonOrbitState>::success(
            rig.current.state);
    }

    const auto pitch = interpolate_component(
        rig.previous.state.pitch_radians,
        rig.current.state.pitch_radians,
        static_cast<double>(alpha));
    if (!pitch) {
        return core::Result<ThirdPersonOrbitState>::failure(
            pitch.error());
    }
    const auto distance = interpolate_component(
        rig.previous.state.boom_distance,
        rig.current.state.boom_distance,
        static_cast<double>(alpha));
    if (!distance) {
        return core::Result<ThirdPersonOrbitState>::failure(
            distance.error());
    }

    const auto yaw_delta = std::remainder(
        static_cast<double>(rig.current.state.yaw_radians) -
            rig.previous.state.yaw_radians,
        static_cast<double>(math::two_pi));
    const auto yaw =
        static_cast<double>(rig.previous.state.yaw_radians) +
        yaw_delta * static_cast<double>(alpha);
    if (!representable_float(yaw)) {
        return core::Result<ThirdPersonOrbitState>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera yaw interpolation exceeded "
                "finite float range"));
    }

    const ThirdPersonOrbitState result{
        .yaw_radians = wrap_yaw(yaw),
        .pitch_radians = pitch.value(),
        .boom_distance = distance.value(),
    };
    if (!valid_orbit(rig.config, result)) {
        return core::Result<ThirdPersonOrbitState>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera interpolation left its authored "
                "orbit bounds"));
    }
    return core::Result<ThirdPersonOrbitState>::success(result);
}

core::Result<HorizontalCameraBasis>
horizontal_camera_basis(const float yaw_radians)
{
    if (!std::isfinite(yaw_radians)) {
        return core::Result<HorizontalCameraBasis>::failure(
            camera_rig_error(
                core::ErrorCode::invalid_argument,
                "Horizontal camera basis requires finite yaw"));
    }
    const auto yaw = static_cast<double>(yaw_radians);
    const HorizontalCameraBasis basis{
        .right = canonical_zero({
            static_cast<float>(std::cos(yaw)),
            0.0F,
            static_cast<float>(std::sin(yaw)),
        }),
        .forward = canonical_zero({
            static_cast<float>(std::sin(yaw)),
            0.0F,
            static_cast<float>(-std::cos(yaw)),
        }),
    };
    if (!math::is_finite(basis.right) ||
        !math::is_finite(basis.forward)) {
        return core::Result<HorizontalCameraBasis>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Horizontal camera basis exceeded finite float "
                "range"));
    }
    return core::Result<HorizontalCameraBasis>::success(basis);
}

core::Result<ThirdPersonCameraPlacement>
build_third_person_camera(
    const ThirdPersonCameraConfig& config,
    const ThirdPersonOrbitState& orbit,
    const math::Float3 target_position,
    const PerspectiveLens& lens,
    const terrain::HeightTileSurface& terrain_surface)
{
    if (!is_valid(config) ||
        !valid_orbit(config, orbit) ||
        !math::is_finite(target_position) ||
        !valid_lens(lens)) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            camera_rig_error(
                core::ErrorCode::invalid_argument,
                "Third-person camera construction requires valid "
                "orbit, target, and perspective lens values"));
    }

    const auto elevated_target_y =
        static_cast<double>(target_position.y) +
        static_cast<double>(config.target_height_offset);
    if (!representable_float(elevated_target_y)) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera target exceeded finite world "
                "coordinates"));
    }
    const auto elevated_target = canonical_zero(math::Float3{
        target_position.x,
        static_cast<float>(elevated_target_y),
        target_position.z,
    });

    const auto forward_result = orbit_forward(orbit);
    if (!forward_result) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            forward_result.error());
    }
    const auto forward = forward_result.value();
    const auto desired_position_result = point_on_boom(
        elevated_target,
        forward,
        static_cast<double>(orbit.boom_distance));
    if (!desired_position_result) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            desired_position_result.error());
    }

    const terrain::Segment3 desired_boom{
        .first_endpoint = elevated_target,
        .second_endpoint = desired_position_result.value(),
    };
    const auto obstruction_result =
        terrain_surface.closest_lod0_point_to_segment(
            desired_boom,
            config.obstruction_clearance);
    if (!obstruction_result) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            obstruction_result.error());
    }

    auto applied_distance = orbit.boom_distance;
    auto terrain_obstructed = false;
    if (obstruction_result.value()) {
        const auto target_clearance_result =
            terrain_surface.closest_lod0_point_to_segment(
                terrain::Segment3{
                    .first_endpoint = elevated_target,
                    .second_endpoint = elevated_target,
                },
                config.obstruction_clearance);
        if (!target_clearance_result) {
            return core::Result<
                ThirdPersonCameraPlacement>::failure(
                    target_clearance_result.error());
        }
        if (target_clearance_result.value()) {
            return core::Result<
                ThirdPersonCameraPlacement>::failure(
                    camera_rig_error(
                        core::ErrorCode::unavailable,
                        "Third-person camera target violates canonical "
                        "terrain obstruction clearance"));
        }

        // A closest point on the complete segment identifies obstruction,
        // but not necessarily the earliest place where a clearance sphere
        // touches a height-field feature. Prefix queries form a monotonic
        // predicate, so fixed-count bisection finds a conservative safe boom
        // without depending on triangle traversal order.
        auto safe_parameter = 0.0;
        auto obstructed_parameter = 1.0;
        constexpr std::uint32_t obstruction_search_iterations = 32U;
        for (std::uint32_t iteration = 0U;
             iteration < obstruction_search_iterations;
             ++iteration) {
            const auto candidate_parameter =
                (safe_parameter + obstructed_parameter) * 0.5;
            const auto candidate_position_result = point_on_boom(
                elevated_target,
                forward,
                static_cast<double>(orbit.boom_distance) *
                    candidate_parameter);
            if (!candidate_position_result) {
                return core::Result<
                    ThirdPersonCameraPlacement>::failure(
                        candidate_position_result.error());
            }
            const auto prefix_result =
                terrain_surface.closest_lod0_point_to_segment(
                    terrain::Segment3{
                        .first_endpoint = elevated_target,
                        .second_endpoint =
                            candidate_position_result.value(),
                    },
                    config.obstruction_clearance);
            if (!prefix_result) {
                return core::Result<
                    ThirdPersonCameraPlacement>::failure(
                        prefix_result.error());
            }
            if (prefix_result.value()) {
                obstructed_parameter = candidate_parameter;
            }
            else {
                safe_parameter = candidate_parameter;
            }
        }

        const auto safe_distance =
            static_cast<double>(orbit.boom_distance) *
            safe_parameter;
        applied_distance = canonical_zero(static_cast<float>(
            std::min(
                safe_distance,
                static_cast<double>(orbit.boom_distance))));
        if (applied_distance > 0.0F) {
            applied_distance = std::nextafter(
                applied_distance,
                0.0F);
        }
        terrain_obstructed = true;
    }

    const auto applied_position_result = point_on_boom(
        elevated_target,
        forward,
        static_cast<double>(applied_distance));
    if (!applied_position_result) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            applied_position_result.error());
    }

    const ThirdPersonCameraPlacement placement{
        .camera = {
            .transform = {
                .position = applied_position_result.value(),
                .yaw_radians = orbit.yaw_radians,
                .pitch_radians = orbit.pitch_radians,
            },
            .lens = lens,
        },
        .target_position = elevated_target,
        .desired_boom_distance = orbit.boom_distance,
        .applied_boom_distance = applied_distance,
        .terrain_obstructed = terrain_obstructed,
    };
    if (!math::is_finite(placement.camera.transform.position) ||
        placement.applied_boom_distance < 0.0F ||
        placement.applied_boom_distance >
            placement.desired_boom_distance) {
        return core::Result<ThirdPersonCameraPlacement>::failure(
            camera_rig_error(
                core::ErrorCode::unavailable,
                "Third-person camera construction produced invalid "
                "boom diagnostics"));
    }
    return core::Result<ThirdPersonCameraPlacement>::success(
        placement);
}

} // namespace shark::world
