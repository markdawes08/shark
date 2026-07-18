#pragma once

#include <shark/core/result.hpp>
#include <shark/render_graph/render_graph.hpp>

#include <functional>

namespace shark::renderer::detail {

inline constexpr render_graph::ExternalResourceId
    frame_back_buffer_external_id{1};
inline constexpr render_graph::ExternalResourceId
    frame_depth_buffer_external_id{2};
inline constexpr render_graph::ExternalResourceId
    frame_checker_texture_external_id{3};
inline constexpr render_graph::ExternalResourceId
    frame_cube_vertex_buffer_external_id{4};
inline constexpr render_graph::ExternalResourceId
    frame_cube_index_buffer_external_id{5};
inline constexpr render_graph::ExternalResourceId
    frame_terrain_vertex_buffer_external_id{6};
inline constexpr render_graph::ExternalResourceId
    frame_terrain_index_buffer_external_id{7};

struct TerrainPassResources final {
    render_graph::ResourceHandle back_buffer;
    render_graph::ResourceHandle depth_buffer;
    render_graph::ResourceHandle vertex_buffer;
    render_graph::ResourceHandle index_buffer;
};

struct TexturedCubePassResources final {
    render_graph::ResourceHandle back_buffer;
    render_graph::ResourceHandle depth_buffer;
    render_graph::ResourceHandle checker_texture;
    render_graph::ResourceHandle vertex_buffer;
    render_graph::ResourceHandle index_buffer;
};

struct SkyboxPassResources final {
    render_graph::ResourceHandle back_buffer;
    render_graph::ResourceHandle depth_buffer;
    render_graph::ResourceHandle vertex_buffer;
    render_graph::ResourceHandle index_buffer;
};

using TerrainPassCallback = std::function<core::Result<void>(
    const render_graph::PassContext&,
    const TerrainPassResources&)>;
using TexturedCubePassCallback = std::function<core::Result<void>(
    const render_graph::PassContext&,
    const TexturedCubePassResources&)>;
using SkyboxPassCallback = std::function<core::Result<void>(
    const render_graph::PassContext&,
    const SkyboxPassResources&)>;

struct FramePipelineCallbacks final {
    TerrainPassCallback terrain;
    TexturedCubePassCallback textured_cube;
    SkyboxPassCallback skybox;
};

// The returned graph owns the callbacks. Captured per-frame backend state must
// therefore remain alive until the caller finishes executing the graph.
[[nodiscard]] core::Result<render_graph::CompiledGraph>
compose_frame_pipeline(FramePipelineCallbacks callbacks);

} // namespace shark::renderer::detail
