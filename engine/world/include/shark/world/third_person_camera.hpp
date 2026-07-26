#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/world/camera.hpp>

#include <cstdint>
#include <type_traits>

namespace shark::world {

inline constexpr float default_third_person_target_height_offset = 0.75F;
inline constexpr float maximum_third_person_target_height_offset = 16.0F;
inline constexpr float default_third_person_minimum_pitch = -1.2F;
inline constexpr float default_third_person_maximum_pitch = 0.35F;
inline constexpr float default_third_person_initial_pitch = -0.25F;
inline constexpr float default_third_person_minimum_boom_distance = 2.0F;
inline constexpr float default_third_person_maximum_boom_distance = 16.0F;
inline constexpr float default_third_person_initial_boom_distance = 9.0F;
inline constexpr float maximum_third_person_boom_distance = 64.0F;
inline constexpr float default_third_person_obstruction_clearance = 0.35F;
inline constexpr float maximum_third_person_obstruction_clearance = 8.0F;

struct ThirdPersonCameraConfig final {
    float target_height_offset{
        default_third_person_target_height_offset};
    float minimum_pitch_radians{
        default_third_person_minimum_pitch};
    float maximum_pitch_radians{
        default_third_person_maximum_pitch};
    float initial_yaw_radians{};
    float initial_pitch_radians{
        default_third_person_initial_pitch};
    float minimum_boom_distance{
        default_third_person_minimum_boom_distance};
    float maximum_boom_distance{
        default_third_person_maximum_boom_distance};
    float initial_boom_distance{
        default_third_person_initial_boom_distance};
    float obstruction_clearance{
        default_third_person_obstruction_clearance};

    [[nodiscard]] friend bool operator==(
        const ThirdPersonCameraConfig&,
        const ThirdPersonCameraConfig&) noexcept = default;
};

struct ThirdPersonOrbitState final {
    float yaw_radians{};
    float pitch_radians{default_third_person_initial_pitch};
    float boom_distance{default_third_person_initial_boom_distance};

    [[nodiscard]] friend bool operator==(
        const ThirdPersonOrbitState&,
        const ThirdPersonOrbitState&) noexcept = default;
};

// Device-neutral orbit input consumed once per emitted fixed tick. Deltas are
// finite changes, not rates; advance clamps pitch and distance to the rig's
// authored bounds and wraps yaw to [-pi, pi).
struct ThirdPersonOrbitDelta final {
    float yaw_radians{};
    float pitch_radians{};
    float boom_distance{};

    [[nodiscard]] friend bool operator==(
        const ThirdPersonOrbitDelta&,
        const ThirdPersonOrbitDelta&) noexcept = default;
};

struct ThirdPersonOrbitSnapshot final {
    ThirdPersonOrbitState state;
    ThirdPersonOrbitDelta consumed_delta;
    std::uint64_t fixed_tick{};

    [[nodiscard]] friend bool operator==(
        const ThirdPersonOrbitSnapshot&,
        const ThirdPersonOrbitSnapshot&) noexcept = default;
};

struct ThirdPersonCameraRig final {
    ThirdPersonCameraConfig config;
    ThirdPersonOrbitSnapshot previous;
    ThirdPersonOrbitSnapshot current;

    [[nodiscard]] friend bool operator==(
        const ThirdPersonCameraRig&,
        const ThirdPersonCameraRig&) noexcept = default;
};

struct HorizontalCameraBasis final {
    math::Float3 right;
    math::Float3 forward;

    [[nodiscard]] friend bool operator==(
        const HorizontalCameraBasis&,
        const HorizontalCameraBasis&) noexcept = default;
};

struct ThirdPersonCameraPlacement final {
    Camera camera;
    math::Float3 target_position;
    float desired_boom_distance{};
    float applied_boom_distance{};
    bool terrain_obstructed{};

    [[nodiscard]] friend bool operator==(
        const ThirdPersonCameraPlacement& left,
        const ThirdPersonCameraPlacement& right) noexcept
    {
        return left.camera.transform.position ==
                right.camera.transform.position &&
            left.camera.transform.yaw_radians ==
                right.camera.transform.yaw_radians &&
            left.camera.transform.pitch_radians ==
                right.camera.transform.pitch_radians &&
            left.camera.lens.vertical_fov_radians ==
                right.camera.lens.vertical_fov_radians &&
            left.camera.lens.near_plane ==
                right.camera.lens.near_plane &&
            left.camera.lens.far_plane ==
                right.camera.lens.far_plane &&
            left.target_position == right.target_position &&
            left.desired_boom_distance ==
                right.desired_boom_distance &&
            left.applied_boom_distance ==
                right.applied_boom_distance &&
            left.terrain_obstructed ==
                right.terrain_obstructed;
    }
};

static_assert(std::is_standard_layout_v<ThirdPersonCameraConfig>);
static_assert(std::is_trivially_copyable_v<ThirdPersonCameraConfig>);
static_assert(std::is_standard_layout_v<ThirdPersonOrbitState>);
static_assert(std::is_trivially_copyable_v<ThirdPersonOrbitState>);
static_assert(std::is_standard_layout_v<ThirdPersonOrbitDelta>);
static_assert(std::is_trivially_copyable_v<ThirdPersonOrbitDelta>);
static_assert(std::is_standard_layout_v<ThirdPersonOrbitSnapshot>);
static_assert(std::is_trivially_copyable_v<ThirdPersonOrbitSnapshot>);
static_assert(std::is_standard_layout_v<ThirdPersonCameraRig>);
static_assert(std::is_trivially_copyable_v<ThirdPersonCameraRig>);
static_assert(std::is_standard_layout_v<HorizontalCameraBasis>);
static_assert(std::is_trivially_copyable_v<HorizontalCameraBasis>);
static_assert(std::is_standard_layout_v<ThirdPersonCameraPlacement>);
static_assert(std::is_trivially_copyable_v<ThirdPersonCameraPlacement>);

[[nodiscard]] bool is_valid(
    const ThirdPersonCameraConfig& config) noexcept;

[[nodiscard]] bool is_valid(
    const ThirdPersonOrbitDelta& delta) noexcept;

[[nodiscard]] bool is_valid(
    const ThirdPersonCameraRig& rig) noexcept;

// Canonicalizes signed zero and wraps the finite initial yaw. Both initial
// snapshots publish tick zero and the exact same authored orbit.
[[nodiscard]] core::Result<ThirdPersonCameraRig>
create_third_person_camera_rig(ThirdPersonCameraConfig config);

// fixed_tick must be exactly current.fixed_tick + 1. Source state and the
// complete candidate are validated before the caller's rig is changed.
[[nodiscard]] core::Result<void> advance_third_person_camera_rig(
    ThirdPersonCameraRig& rig,
    ThirdPersonOrbitDelta delta,
    std::uint64_t fixed_tick);

// Collapses only the presentation interpolation interval after an external
// time-baseline discontinuity. Config, tick ordering, consumed deltas, and the
// authoritative current snapshot remain unchanged. Invalid source state is
// rejected without changing the caller's rig.
[[nodiscard]] core::Result<void>
collapse_third_person_camera_interpolation(
    ThirdPersonCameraRig& rig);

// Produces presentation-only orbit state. Pitch and distance are linear while
// yaw follows the shortest wrapped arc.
[[nodiscard]] core::Result<ThirdPersonOrbitState>
interpolate_third_person_camera_rig(
    const ThirdPersonCameraRig& rig,
    float alpha);

// Yaw alone defines character movement on the horizontal world plane. At yaw
// zero, right is +X and forward is -Z.
[[nodiscard]] core::Result<HorizontalCameraBasis>
horizontal_camera_basis(float yaw_radians);

// target_position is authoritative world state (normally the player capsule
// center) and is never changed. The configured target-height offset is applied
// before constructing the desired boom. Canonical LOD0 terrain can only
// shorten that boom; the supplied perspective lens is preserved byte-for-byte.
[[nodiscard]] core::Result<ThirdPersonCameraPlacement>
build_third_person_camera(
    const ThirdPersonCameraConfig& config,
    const ThirdPersonOrbitState& orbit,
    math::Float3 target_position,
    const PerspectiveLens& lens,
    const terrain::HeightTileSurface& terrain_surface);

} // namespace shark::world
