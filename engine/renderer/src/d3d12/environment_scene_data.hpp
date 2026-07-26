#pragma once

#include <shark/core/math.hpp>
#include <shark/renderer/renderer.hpp>

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
inline constexpr float debug_capsule_parameter_seam = 0.5F;

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
make_debug_capsule_transform(
    const DebugCapsuleProxy& proxy) noexcept
{
    return {
        .orientation = proxy.orientation,
        .world_position = proxy.world_position,
        .radius = proxy.radius,
        .half_segment_length = proxy.half_segment_length,
    };
}

[[nodiscard]] inline bool valid_debug_capsule_proxy(
    const DebugCapsuleProxy& proxy) noexcept
{
    if (!proxy.enabled) {
        return true;
    }
    return math::is_finite(proxy.world_position) &&
        math::is_finite(proxy.orientation) &&
        math::is_unit(proxy.orientation) &&
        std::isfinite(proxy.radius) &&
        proxy.radius > 0.0F &&
        proxy.radius <= maximum_debug_capsule_radius &&
        std::isfinite(proxy.half_segment_length) &&
        proxy.half_segment_length > 0.0F &&
        proxy.half_segment_length <=
            maximum_debug_capsule_half_segment_length;
}

struct EnvironmentProxySurface final {
    math::Float3 local_position{};
    math::Float3 local_normal{};
    bool debug_capsule{};
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
            .debug_capsule = false,
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
    if (unit_direction.y >= debug_capsule_parameter_seam) {
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
             -debug_capsule_parameter_seam) {
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
                 debug_capsule_parameter_seam),
            normal.z * radius,
        };
    }
    return {
        .local_position = position,
        .local_normal = normal,
        .debug_capsule = true,
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
