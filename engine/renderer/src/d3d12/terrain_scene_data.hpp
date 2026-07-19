#pragma once

#include "cube_scene_data.hpp"

#include <shark/renderer/renderer.hpp>

#include <directx/d3d12.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shark::renderer::d3d12::detail {

inline constexpr std::uint32_t terrain_vertex_stride =
    sizeof(float) * 6U;
inline constexpr std::uint32_t terrain_position_offset = 0;
inline constexpr std::uint32_t terrain_normal_offset =
    sizeof(float) * 3U;

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2>
    terrain_input_elements{{
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            terrain_position_offset,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "NORMAL",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            terrain_normal_offset,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
    }};
inline constexpr D3D12_INPUT_LAYOUT_DESC terrain_input_layout{
    terrain_input_elements.data(),
    static_cast<UINT>(terrain_input_elements.size()),
};

inline constexpr DXGI_FORMAT terrain_index_format =
    DXGI_FORMAT_R16_UINT;
inline constexpr D3D12_FILL_MODE terrain_solid_fill_mode =
    D3D12_FILL_MODE_SOLID;
inline constexpr D3D12_FILL_MODE terrain_wireframe_fill_mode =
    D3D12_FILL_MODE_WIREFRAME;
inline constexpr D3D12_PRIMITIVE_TOPOLOGY terrain_topology =
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
inline constexpr D3D12_PRIMITIVE_TOPOLOGY terrain_bounds_topology =
    D3D_PRIMITIVE_TOPOLOGY_LINELIST;
inline constexpr D3D12_PRIMITIVE_TOPOLOGY
    terrain_query_marker_topology =
        D3D_PRIMITIVE_TOPOLOGY_LINELIST;
inline constexpr D3D12_DEPTH_WRITE_MASK terrain_depth_write_mask =
    cube_depth_write_mask;
inline constexpr D3D12_DEPTH_WRITE_MASK terrain_bounds_depth_write_mask =
    D3D12_DEPTH_WRITE_MASK_ZERO;
inline constexpr D3D12_COMPARISON_FUNC terrain_depth_comparison =
    cube_depth_comparison;
inline constexpr std::uint32_t terrain_query_marker_vertex_count = 6;
inline constexpr std::uint32_t terrain_query_marker_index_count = 6;
inline constexpr std::uint32_t terrain_chunk_bounds_vertex_count = 8;
inline constexpr std::uint32_t terrain_chunk_bounds_index_count = 24;
inline constexpr std::uint32_t terrain_maximum_chunk_count = 4'096;

inline constexpr std::uint32_t terrain_material_layer_count = 2;
inline constexpr std::uint32_t terrain_material_texture_count = 3;
inline constexpr std::uint32_t terrain_material_maximum_dimension = 256;
inline constexpr std::uint32_t terrain_material_root_constant_count = 8;
inline constexpr std::uint32_t terrain_camera_root_parameter = 0;
inline constexpr std::uint32_t terrain_material_constants_root_parameter = 1;
inline constexpr std::uint32_t terrain_material_table_root_parameter = 2;
inline constexpr std::uint32_t terrain_material_albedo_descriptor = 0;
inline constexpr std::uint32_t terrain_material_normal_descriptor = 1;
inline constexpr std::uint32_t terrain_material_roughness_descriptor = 2;
inline constexpr std::uint32_t terrain_environment_irradiance_descriptor = 3;
inline constexpr std::uint32_t terrain_environment_specular_descriptor = 4;
inline constexpr std::uint32_t terrain_environment_brdf_descriptor = 5;
inline constexpr std::uint32_t terrain_shader_texture_count = 6;
inline constexpr DXGI_FORMAT terrain_material_albedo_format =
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
inline constexpr DXGI_FORMAT terrain_material_normal_format =
    DXGI_FORMAT_R8G8B8A8_UNORM;
inline constexpr DXGI_FORMAT terrain_material_roughness_format =
    DXGI_FORMAT_R8G8B8A8_UNORM;

// T-003 deliberately fixes these fixture-scale controls. A data-driven
// material system belongs to a later content-pipeline increment.
inline constexpr float terrain_material_repetitions_per_meter = 0.5F;
inline constexpr float terrain_material_normal_strength = 0.65F;
inline constexpr float terrain_material_slope_blend_start = 0.05F;
inline constexpr float terrain_material_slope_blend_end = 0.18F;
inline constexpr float terrain_material_height_blend_start = -1.5F;
inline constexpr float terrain_material_height_blend_end = -0.4F;
inline constexpr float terrain_material_height_rock_contribution = 0.4F;

struct TerrainMaterialRootConstants final {
    math::Float3 camera_world_position{};
    std::uint32_t material_view{
        static_cast<std::uint32_t>(TerrainMaterialView::shaded)};
    std::uint32_t environment_lighting_mode{
        static_cast<std::uint32_t>(
            EnvironmentLightingMode::image_based)};
    float specular_max_lod{5.0F};
    std::uint32_t reserved_zero{};
    std::uint32_t reserved_one{};
};

[[nodiscard]] constexpr bool valid_terrain_material_view(
    const TerrainMaterialView view) noexcept
{
    return view == TerrainMaterialView::shaded ||
        view == TerrainMaterialView::material_weights ||
        view == TerrainMaterialView::shading_normal;
}

[[nodiscard]] constexpr float terrain_material_smoothstep(
    const float edge0,
    const float edge1,
    const float value) noexcept
{
    const auto amount = std::clamp(
        (value - edge0) / (edge1 - edge0),
        0.0F,
        1.0F);
    return amount * amount * (3.0F - 2.0F * amount);
}

[[nodiscard]] constexpr float terrain_material_rock_weight(
    const float macro_normal_y,
    const float world_height) noexcept
{
    const auto slope = 1.0F -
        std::clamp(macro_normal_y, 0.0F, 1.0F);
    const auto slope_rock = terrain_material_smoothstep(
        terrain_material_slope_blend_start,
        terrain_material_slope_blend_end,
        slope);
    const auto height_rock =
        terrain_material_height_rock_contribution *
        terrain_material_smoothstep(
            terrain_material_height_blend_start,
            terrain_material_height_blend_end,
            world_height);
    return 1.0F -
        (1.0F - slope_rock) * (1.0F - height_rock);
}

static_assert(terrain_vertex_stride == 24);
static_assert(terrain_normal_offset == 12);
static_assert(std::is_standard_layout_v<TerrainMaterialRootConstants>);
static_assert(sizeof(TerrainMaterialRootConstants) == 32);
static_assert(
    terrain_material_texture_count ==
    terrain_material_roughness_descriptor + 1U);
static_assert(
    terrain_shader_texture_count ==
    terrain_environment_brdf_descriptor + 1U);

} // namespace shark::renderer::d3d12::detail
