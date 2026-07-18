#pragma once

#include "cube_scene_data.hpp"

#include <directx/d3d12.h>

#include <array>
#include <cstddef>
#include <cstdint>

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

static_assert(terrain_vertex_stride == 24);
static_assert(terrain_normal_offset == 12);

} // namespace shark::renderer::d3d12::detail
