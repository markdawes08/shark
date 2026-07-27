#pragma once

#include <shark/core/math.hpp>
#include <shark/renderer/renderer.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <vector>

namespace shark::renderer::d3d12::detail {

inline constexpr std::uint32_t material_sphere_slices = 24;
inline constexpr std::uint32_t material_sphere_rings = 11;
inline constexpr std::uint32_t material_sphere_vertex_count =
    2U + material_sphere_slices * material_sphere_rings;
inline constexpr std::uint32_t material_sphere_index_count =
    material_sphere_slices * 3U * 2U +
    (material_sphere_rings - 1U) *
        material_sphere_slices * 6U;
inline constexpr math::Float3 material_sphere_center{
    3.0F,
    1.25F,
    -1.0F,
};
inline constexpr float material_sphere_radius = 1.0F;
inline constexpr std::uint32_t
    material_sphere_transform_root_parameter = 3;
inline constexpr std::uint32_t
    material_sphere_transform_root_constant_count = 9;
inline constexpr float capsule_parameter_seam = 0.5F;
inline constexpr float minimum_placeholder_avatar_body_pitch =
    -1.25F;
inline constexpr float maximum_placeholder_avatar_body_pitch =
    0.25F;
inline constexpr float
    maximum_placeholder_avatar_body_vertical_offset = 0.40F;
inline constexpr float maximum_placeholder_avatar_torso_pitch =
    0.20F;
inline constexpr float maximum_placeholder_avatar_arm_pitch = 1.0F;
inline constexpr float maximum_placeholder_avatar_leg_pitch = 0.90F;

inline constexpr math::Float3 placeholder_avatar_torso_center{
    0.0F,
    0.20F,
    0.0F,
};
inline constexpr float placeholder_avatar_torso_radius = 0.24F;
inline constexpr float
    placeholder_avatar_torso_half_segment_length = 0.27F;
inline constexpr math::Float3 placeholder_avatar_head_center{
    0.0F,
    0.79F,
    0.0F,
};
inline constexpr float placeholder_avatar_head_radius = 0.20F;
inline constexpr math::Float3 placeholder_avatar_left_arm_center{
    -0.34F,
    0.20F,
    0.0F,
};
inline constexpr math::Float3 placeholder_avatar_right_arm_center{
    0.34F,
    0.20F,
    0.0F,
};
inline constexpr float placeholder_avatar_arm_radius = 0.10F;
inline constexpr float
    placeholder_avatar_arm_half_segment_length = 0.32F;
inline constexpr math::Float3 placeholder_avatar_left_leg_center{
    -0.12F,
    -0.52F,
    0.0F,
};
inline constexpr math::Float3 placeholder_avatar_right_leg_center{
    0.12F,
    -0.52F,
    0.0F,
};
inline constexpr float placeholder_avatar_leg_radius = 0.10F;
inline constexpr float
    placeholder_avatar_leg_half_segment_length = 0.31F;

struct MaterialSphereTransformRootConstants final {
    math::Quaternion orientation{};
    math::Float3 world_position{};
    float radius{material_sphere_radius};
    float half_segment_length{};
};

[[nodiscard]] constexpr MaterialSphereTransformRootConstants
make_material_sphere_transform(
    const math::Quaternion orientation,
    const math::Float3 world_position) noexcept
{
    return {
        .orientation = orientation,
        .world_position = world_position,
        .radius = material_sphere_radius,
        .half_segment_length = 0.0F,
    };
}

[[nodiscard]] constexpr MaterialSphereTransformRootConstants
make_environment_proxy_transform(
    const math::Quaternion orientation,
    const math::Float3 world_position,
    const float radius,
    const float half_segment_length) noexcept
{
    return {
        .orientation = orientation,
        .world_position = world_position,
        .radius = radius,
        .half_segment_length = half_segment_length,
    };
}

[[nodiscard]] inline bool in_inclusive_range(
    const float value,
    const float minimum,
    const float maximum) noexcept
{
    return std::isfinite(value) &&
        value >= minimum &&
        value <= maximum;
}

[[nodiscard]] inline bool valid_placeholder_avatar_pose(
    const PlaceholderAvatarPose& pose) noexcept
{
    return in_inclusive_range(
               pose.body_pitch_radians,
               minimum_placeholder_avatar_body_pitch,
               maximum_placeholder_avatar_body_pitch) &&
        in_inclusive_range(
            pose.body_vertical_offset,
            0.0F,
            maximum_placeholder_avatar_body_vertical_offset) &&
        in_inclusive_range(
            pose.torso_pitch_radians,
            -maximum_placeholder_avatar_torso_pitch,
            maximum_placeholder_avatar_torso_pitch) &&
        in_inclusive_range(
            pose.left_arm_pitch_radians,
            -maximum_placeholder_avatar_arm_pitch,
            maximum_placeholder_avatar_arm_pitch) &&
        in_inclusive_range(
            pose.right_arm_pitch_radians,
            -maximum_placeholder_avatar_arm_pitch,
            maximum_placeholder_avatar_arm_pitch) &&
        in_inclusive_range(
            pose.left_leg_pitch_radians,
            -maximum_placeholder_avatar_leg_pitch,
            maximum_placeholder_avatar_leg_pitch) &&
        in_inclusive_range(
            pose.right_leg_pitch_radians,
            -maximum_placeholder_avatar_leg_pitch,
            maximum_placeholder_avatar_leg_pitch);
}

[[nodiscard]] inline math::Quaternion pitch_orientation(
    const float pitch_radians) noexcept
{
    const auto half_pitch = pitch_radians * 0.5F;
    return {
        std::sin(half_pitch),
        0.0F,
        0.0F,
        std::cos(half_pitch),
    };
}

// Composition order matches the shader's vector rotation:
// rotate(parent * local, v) == rotate(parent, rotate(local, v)).
[[nodiscard]] constexpr math::Quaternion compose_orientation(
    const math::Quaternion parent,
    const math::Quaternion local) noexcept
{
    return {
        parent.w * local.x +
            parent.x * local.w +
            parent.y * local.z -
            parent.z * local.y,
        parent.w * local.y -
            parent.x * local.z +
            parent.y * local.w +
            parent.z * local.x,
        parent.w * local.z +
            parent.x * local.y -
            parent.y * local.x +
            parent.z * local.w,
        parent.w * local.w -
            parent.x * local.x -
            parent.y * local.y -
            parent.z * local.z,
    };
}

[[nodiscard]] constexpr math::Float3 avatar_add(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z,
    };
}

[[nodiscard]] constexpr math::Float3 avatar_subtract(
    const math::Float3 first,
    const math::Float3 second) noexcept
{
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z,
    };
}

[[nodiscard]] inline MaterialSphereTransformRootConstants
make_placeholder_avatar_part(
    const PlaceholderAvatarProxy& proxy,
    const math::Quaternion body_orientation,
    const math::Quaternion local_orientation,
    const math::Float3 joint_position,
    const math::Float3 rest_center,
    const float radius,
    const float half_segment_length) noexcept
{
    const auto joint_offset =
        avatar_subtract(rest_center, joint_position);
    const auto posed_center = avatar_add(
        joint_position,
        math::rotate(local_orientation, joint_offset));
    auto body_center = math::rotate(body_orientation, posed_center);
    body_center.y += proxy.pose.body_vertical_offset;
    const auto world_center = avatar_add(
        proxy.world_position,
        math::rotate(proxy.orientation, body_center));
    const auto orientation = compose_orientation(
        proxy.orientation,
        compose_orientation(body_orientation, local_orientation));
    return make_environment_proxy_transform(
        orientation,
        world_center,
        radius,
        half_segment_length);
}

[[nodiscard]] inline std::array<
    MaterialSphereTransformRootConstants,
    placeholder_avatar_part_count>
make_placeholder_avatar_parts(
    const PlaceholderAvatarProxy& proxy) noexcept
{
    const auto body_orientation =
        pitch_orientation(proxy.pose.body_pitch_radians);
    const auto torso_orientation =
        pitch_orientation(proxy.pose.torso_pitch_radians);
    const auto left_arm_orientation =
        pitch_orientation(proxy.pose.left_arm_pitch_radians);
    const auto right_arm_orientation =
        pitch_orientation(proxy.pose.right_arm_pitch_radians);
    const auto left_leg_orientation =
        pitch_orientation(proxy.pose.left_leg_pitch_radians);
    const auto right_leg_orientation =
        pitch_orientation(proxy.pose.right_leg_pitch_radians);

    constexpr math::Float3 waist_joint{
        0.0F,
        placeholder_avatar_torso_center.y -
            placeholder_avatar_torso_half_segment_length -
            placeholder_avatar_torso_radius,
        0.0F,
    };
    constexpr math::Float3 left_shoulder_joint{
        placeholder_avatar_left_arm_center.x,
        placeholder_avatar_left_arm_center.y +
            placeholder_avatar_arm_half_segment_length +
            placeholder_avatar_arm_radius,
        0.0F,
    };
    constexpr math::Float3 right_shoulder_joint{
        placeholder_avatar_right_arm_center.x,
        placeholder_avatar_right_arm_center.y +
            placeholder_avatar_arm_half_segment_length +
            placeholder_avatar_arm_radius,
        0.0F,
    };
    constexpr math::Float3 left_hip_joint{
        placeholder_avatar_left_leg_center.x,
        placeholder_avatar_left_leg_center.y +
            placeholder_avatar_leg_half_segment_length +
            placeholder_avatar_leg_radius,
        0.0F,
    };
    constexpr math::Float3 right_hip_joint{
        placeholder_avatar_right_leg_center.x,
        placeholder_avatar_right_leg_center.y +
            placeholder_avatar_leg_half_segment_length +
            placeholder_avatar_leg_radius,
        0.0F,
    };

    return {{
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            torso_orientation,
            waist_joint,
            placeholder_avatar_torso_center,
            placeholder_avatar_torso_radius,
            placeholder_avatar_torso_half_segment_length),
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            torso_orientation,
            waist_joint,
            placeholder_avatar_head_center,
            placeholder_avatar_head_radius,
            0.0F),
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            left_arm_orientation,
            left_shoulder_joint,
            placeholder_avatar_left_arm_center,
            placeholder_avatar_arm_radius,
            placeholder_avatar_arm_half_segment_length),
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            right_arm_orientation,
            right_shoulder_joint,
            placeholder_avatar_right_arm_center,
            placeholder_avatar_arm_radius,
            placeholder_avatar_arm_half_segment_length),
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            left_leg_orientation,
            left_hip_joint,
            placeholder_avatar_left_leg_center,
            placeholder_avatar_leg_radius,
            placeholder_avatar_leg_half_segment_length),
        make_placeholder_avatar_part(
            proxy,
            body_orientation,
            right_leg_orientation,
            right_hip_joint,
            placeholder_avatar_right_leg_center,
            placeholder_avatar_leg_radius,
            placeholder_avatar_leg_half_segment_length),
    }};
}

[[nodiscard]] inline bool valid_environment_proxy_transform(
    const MaterialSphereTransformRootConstants& transform) noexcept
{
    return math::is_finite(transform.orientation) &&
        math::is_unit(transform.orientation) &&
        math::is_finite(transform.world_position) &&
        std::isfinite(transform.radius) &&
        transform.radius > 0.0F &&
        std::isfinite(transform.half_segment_length) &&
        transform.half_segment_length >= 0.0F;
}

[[nodiscard]] inline bool valid_placeholder_avatar_proxy(
    const PlaceholderAvatarProxy& proxy) noexcept
{
    if (!proxy.enabled) {
        return true;
    }
    if (!math::is_finite(proxy.world_position) ||
        !math::is_finite(proxy.orientation) ||
        !math::is_unit(proxy.orientation) ||
        !valid_placeholder_avatar_pose(proxy.pose)) {
        return false;
    }
    const auto parts = make_placeholder_avatar_parts(proxy);
    for (const auto& part : parts) {
        if (!valid_environment_proxy_transform(part)) {
            return false;
        }
    }
    return true;
}

struct EnvironmentProxySurface final {
    math::Float3 local_position{};
    math::Float3 local_normal{};
    bool capsule{};
};

[[nodiscard]] inline EnvironmentProxySurface
deform_environment_proxy(
    const math::Float3 unit_direction,
    const float radius,
    const float half_segment_length) noexcept
{
    if (half_segment_length == 0.0F) {
        return {
            .local_position = {
                unit_direction.x * radius,
                unit_direction.y * radius,
                unit_direction.z * radius,
            },
            .local_normal = unit_direction,
            .capsule = false,
        };
    }

    const auto normalize_or = [](
        const math::Float3 value,
        const math::Float3 fallback) noexcept {
        const auto length_squared =
            value.x * value.x +
            value.y * value.y +
            value.z * value.z;
        if (!std::isfinite(length_squared) ||
            length_squared <= 1.0e-12F) {
            return fallback;
        }
        const auto inverse_length =
            1.0F / std::sqrt(length_squared);
        return math::Float3{
            value.x * inverse_length,
            value.y * inverse_length,
            value.z * inverse_length,
        };
    };

    math::Float3 normal{};
    math::Float3 position{};
    if (unit_direction.y >= capsule_parameter_seam) {
        normal = normalize_or(
            {
                unit_direction.x,
                2.0F * unit_direction.y - 1.0F,
                unit_direction.z,
            },
            {0.0F, 1.0F, 0.0F});
        position = {
            normal.x * radius,
            half_segment_length + normal.y * radius,
            normal.z * radius,
        };
    }
    else if (unit_direction.y <=
             -capsule_parameter_seam) {
        normal = normalize_or(
            {
                unit_direction.x,
                2.0F * unit_direction.y + 1.0F,
                unit_direction.z,
            },
            {0.0F, -1.0F, 0.0F});
        position = {
            normal.x * radius,
            -half_segment_length + normal.y * radius,
            normal.z * radius,
        };
    }
    else {
        normal = normalize_or(
            {
                unit_direction.x,
                0.0F,
                unit_direction.z,
            },
            {1.0F, 0.0F, 0.0F});
        position = {
            normal.x * radius,
            half_segment_length *
                (unit_direction.y /
                 capsule_parameter_seam),
            normal.z * radius,
        };
    }
    return {
        .local_position = position,
        .local_normal = normal,
        .capsule = true,
    };
}

struct EnvironmentVertex final {
    math::Float3 position;
    math::Float3 normal;
};

struct MaterialSphereMesh final {
    std::vector<EnvironmentVertex> vertices;
    std::vector<std::uint16_t> indices;
};

[[nodiscard]] inline MaterialSphereMesh make_material_sphere_mesh()
{
    MaterialSphereMesh mesh;
    mesh.vertices.reserve(material_sphere_vertex_count);
    mesh.indices.reserve(material_sphere_index_count);

    const auto append_vertex = [&mesh](const math::Float3 normal) {
        mesh.vertices.push_back(EnvironmentVertex{
            {
                material_sphere_center.x +
                    normal.x * material_sphere_radius,
                material_sphere_center.y +
                    normal.y * material_sphere_radius,
                material_sphere_center.z +
                    normal.z * material_sphere_radius,
            },
            normal,
        });
    };

    append_vertex({0.0F, 1.0F, 0.0F});
    for (std::uint32_t ring = 1;
         ring <= material_sphere_rings;
         ++ring) {
        const auto latitude =
            std::numbers::pi_v<float> *
            static_cast<float>(ring) /
            static_cast<float>(material_sphere_rings + 1U);
        const auto y = std::cos(latitude);
        const auto radius = std::sin(latitude);
        for (std::uint32_t slice = 0;
             slice < material_sphere_slices;
             ++slice) {
            const auto longitude =
                2.0F * std::numbers::pi_v<float> *
                static_cast<float>(slice) /
                static_cast<float>(material_sphere_slices);
            append_vertex({
                radius * std::cos(longitude),
                y,
                -radius * std::sin(longitude),
            });
        }
    }
    append_vertex({0.0F, -1.0F, 0.0F});

    const auto ring_vertex = [](const std::uint32_t ring,
                                const std::uint32_t slice) {
        return static_cast<std::uint16_t>(
            1U +
            ring * material_sphere_slices +
            slice % material_sphere_slices);
    };
    for (std::uint32_t slice = 0;
         slice < material_sphere_slices;
         ++slice) {
        const auto next = (slice + 1U) % material_sphere_slices;
        mesh.indices.push_back(0);
        mesh.indices.push_back(ring_vertex(0, next));
        mesh.indices.push_back(ring_vertex(0, slice));
    }

    for (std::uint32_t ring = 0;
         ring + 1U < material_sphere_rings;
         ++ring) {
        for (std::uint32_t slice = 0;
             slice < material_sphere_slices;
             ++slice) {
            const auto next = (slice + 1U) % material_sphere_slices;
            const auto upper = ring_vertex(ring, slice);
            const auto upper_next = ring_vertex(ring, next);
            const auto lower = ring_vertex(ring + 1U, slice);
            const auto lower_next = ring_vertex(ring + 1U, next);
            mesh.indices.insert(
                mesh.indices.end(),
                {
                    upper,
                    upper_next,
                    lower,
                    upper_next,
                    lower_next,
                    lower,
                });
        }
    }

    const auto bottom = static_cast<std::uint16_t>(
        material_sphere_vertex_count - 1U);
    const auto final_ring = material_sphere_rings - 1U;
    for (std::uint32_t slice = 0;
         slice < material_sphere_slices;
         ++slice) {
        const auto next = (slice + 1U) % material_sphere_slices;
        mesh.indices.push_back(ring_vertex(final_ring, slice));
        mesh.indices.push_back(ring_vertex(final_ring, next));
        mesh.indices.push_back(bottom);
    }
    return mesh;
}

static_assert(sizeof(EnvironmentVertex) == sizeof(float) * 6U);
static_assert(
    std::is_standard_layout_v<
        MaterialSphereTransformRootConstants>);
static_assert(sizeof(MaterialSphereTransformRootConstants) == 36);
static_assert(
    sizeof(MaterialSphereTransformRootConstants) ==
    material_sphere_transform_root_constant_count * sizeof(float));
static_assert(
    offsetof(MaterialSphereTransformRootConstants, orientation) == 0);
static_assert(
    offsetof(MaterialSphereTransformRootConstants, world_position) == 16);
static_assert(
    offsetof(MaterialSphereTransformRootConstants, radius) == 28);
static_assert(
    offsetof(
        MaterialSphereTransformRootConstants,
        half_segment_length) == 32);
static_assert(material_sphere_vertex_count == 266);
static_assert(material_sphere_index_count == 1'584);

} // namespace shark::renderer::d3d12::detail
