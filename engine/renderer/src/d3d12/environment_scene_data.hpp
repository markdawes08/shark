#pragma once

#include <shark/core/math.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
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
static_assert(material_sphere_vertex_count == 266);
static_assert(material_sphere_index_count == 1'584);

} // namespace shark::renderer::d3d12::detail
