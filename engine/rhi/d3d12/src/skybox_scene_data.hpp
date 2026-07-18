#pragma once

#include "cube_scene_data.hpp"

#include <directx/d3d12.h>

#include <cstdint>

namespace shark::rhi::d3d12::detail {

// Reversed-Z clears the depth target to zero. The skybox emits that exact far
// depth, passes only where no nearer geometry was written, and never changes
// the depth buffer itself.
inline constexpr float skybox_clip_depth = 0.0F;
inline constexpr D3D12_COMPARISON_FUNC skybox_depth_comparison =
    D3D12_COMPARISON_FUNC_GREATER_EQUAL;
inline constexpr D3D12_DEPTH_WRITE_MASK skybox_depth_write_mask =
    D3D12_DEPTH_WRITE_MASK_ZERO;

// The procedural daylight sky has no second geometry allocation. It draws the
// existing 24-vertex cube from inside with the same twelve indexed triangles.
inline constexpr const auto& skybox_indices = cube_indices;
inline constexpr std::uint32_t skybox_index_count =
    static_cast<std::uint32_t>(skybox_indices.size());

static_assert(skybox_clip_depth == cube_depth_clear_value);
static_assert(skybox_depth_comparison == cube_depth_comparison);
static_assert(skybox_index_count == 36);

} // namespace shark::rhi::d3d12::detail
