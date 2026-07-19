#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace shark::rhi::d3d12 {
class Device;
}

namespace shark::renderer {

struct RenderExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend bool operator==(
        const RenderExtent&,
        const RenderExtent&) noexcept = default;
};

struct ClearColor final {
    float red{0.02F};
    float green{0.08F};
    float blue{0.14F};
    float alpha{1.0F};
};

struct ShaderBytecodeView final {
    const void* data{};
    std::size_t size{};
};

enum class TextureDataFormat : std::uint8_t {
    rgba8_unorm = 1,
    rgba8_unorm_srgb,
    rgba32_float,
};

struct TextureSubresourceDataView final {
    const std::byte* data{};
    std::size_t data_size{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
};

// Subresources are ordered face-major, then mip-minor:
// +X mips, -X mips, +Y mips, -Y mips, +Z mips, -Z mips.
// Renderer consumes these borrowed views synchronously during create().
struct TextureCubeUploadView final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_levels{};
    TextureDataFormat format{};
    const TextureSubresourceDataView* subresources{};
    std::size_t subresource_count{};
};

// A single two-dimensional texture with mip-minor subresources. Renderer
// consumes these borrowed views synchronously during create().
struct Texture2DUploadView final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_levels{};
    TextureDataFormat format{};
    const TextureSubresourceDataView* subresources{};
    std::size_t subresource_count{};
};

// Subresources are ordered layer-major, then mip-minor. Renderer consumes
// these borrowed views synchronously during create().
struct Texture2DArrayUploadView final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t array_layers{};
    std::uint32_t mip_levels{};
    TextureDataFormat format{};
    const TextureSubresourceDataView* subresources{};
    std::size_t subresource_count{};
};

struct TerrainChunkUploadView final {
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::uint32_t coarse_first_index{};
    std::uint32_t coarse_index_count{};
    math::Float3 bounds_minimum{};
    math::Float3 bounds_maximum{};

    // Maximum vertical separation from the canonical LOD0 surface, in
    // world-space meters.
    double maximum_geometric_error{};
};

// Terrain chunks, their diagnostic bounds, and the query marker are copied
// synchronously during create(). Every vertex stream uses interleaved float3
// POSITION / float3 NORMAL data; every index stream uses uint16_t. Chunk
// LOD0 chunk ranges form one contiguous prefix; coarse ranges form the
// contiguous suffix and together cover the full triangle index stream.
// Bounds contain exactly eight vertices and 24 local indices per chunk.
struct TerrainMeshUploadView final {
    const void* vertices{};
    std::size_t vertex_count{};
    std::size_t vertex_stride{};
    const std::uint16_t* indices{};
    std::size_t index_count{};
    const TerrainChunkUploadView* chunks{};
    std::size_t chunk_count{};
    const void* bounds_vertices{};
    std::size_t bounds_vertex_count{};
    std::size_t bounds_vertex_stride{};
    const std::uint16_t* bounds_indices{};
    std::size_t bounds_index_count{};
    const void* query_marker_vertices{};
    std::size_t query_marker_vertex_count{};
    std::size_t query_marker_vertex_stride{};
    const std::uint16_t* query_marker_indices{};
    std::size_t query_marker_index_count{};
};

struct TerrainMaterialUploadView final {
    Texture2DArrayUploadView albedo;
    Texture2DArrayUploadView normal;
    Texture2DArrayUploadView roughness;
};

// S-003 consumes deterministic, offline-ready derived lighting data rather
// than performing expensive environment convolution in the frame loop.
struct EnvironmentLightingUploadView final {
    TextureCubeUploadView radiance;
    TextureCubeUploadView diffuse_irradiance;
    TextureCubeUploadView prefiltered_specular;
    Texture2DUploadView brdf_lut;
};

// A bounded, presentation-only horizontal water surface. The visible domain
// is the intersection of the warped rho <= 1 field and this procedural
// six-vertex quad. The explicit quad selects the intended local basin even
// when the polynomial warp has remote mathematical solutions.
struct WaterSurfaceSettings final {
    math::Float3 center{};
    float semi_axis_x{};
    float semi_axis_z{};
    float x_warp_square_offset{};
    float x_warp_divisor{};
    float z_warp_square_offset{};
    float z_warp_divisor{};
    float core_depth{};
    float render_half_extent_x{};
    float render_half_extent_z{};

    [[nodiscard]] friend bool operator==(
        const WaterSurfaceSettings&,
        const WaterSurfaceSettings&) noexcept = default;
};

// Procedural-daylight fallback controls for the environment renderer
// boundary. Six float4-compatible rows are copied directly after the two
// camera matrices in b0.
struct alignas(16) DaylightSettings final {
    // Unit vector from a shaded surface or sky sample toward the sun.
    math::Float3 direction_to_sun{
        0.30F,
        0.08F,
        -0.95057877F,
    };
    float sun_disk_outer_cosine{0.99965732F};

    math::Float3 sun_color{1.0F, 0.90F, 0.68F};
    float sun_disk_inner_cosine{0.99990252F};

    math::Float3 zenith_color{0.055F, 0.20F, 0.52F};
    float sky_gradient_exponent{0.65F};

    math::Float3 horizon_color{0.52F, 0.72F, 0.92F};
    float ambient_strength{1.0F};

    math::Float3 nadir_color{0.08F, 0.10F, 0.12F};
    float sun_halo_outer_cosine{0.97814760F};

    math::Float3 sky_ambient_color{0.32F, 0.40F, 0.52F};
    float sun_intensity{1.1F};
};

static_assert(sizeof(DaylightSettings) == 96);
static_assert(alignof(DaylightSettings) == 16);

enum class TerrainRenderMode : std::uint8_t {
    solid = 1,
    wireframe,
};

enum class TerrainMaterialView : std::uint32_t {
    shaded = 1,
    material_weights,
    shading_normal,
};

enum class EnvironmentLightingMode : std::uint32_t {
    procedural_daylight = 1,
    image_based,
};

struct RendererConfig final {
    void* native_window{};
    RenderExtent extent{1280, 720};
    ClearColor clear_color{};
    ShaderBytecodeView textured_cube_vertex_shader{};
    ShaderBytecodeView textured_cube_pixel_shader{};
    ShaderBytecodeView skybox_vertex_shader{};
    ShaderBytecodeView skybox_pixel_shader{};
    ShaderBytecodeView terrain_vertex_shader{};
    ShaderBytecodeView terrain_pixel_shader{};
    ShaderBytecodeView water_vertex_shader{};
    ShaderBytecodeView water_pixel_shader{};
    ShaderBytecodeView material_sphere_vertex_shader{};
    ShaderBytecodeView material_sphere_pixel_shader{};
    ShaderBytecodeView tone_map_vertex_shader{};
    ShaderBytecodeView tone_map_pixel_shader{};
    TextureCubeUploadView startup_cubemap{};
    TerrainMeshUploadView terrain_mesh{};
    TerrainMaterialUploadView terrain_materials{};
    EnvironmentLightingUploadView environment_lighting{};
    WaterSurfaceSettings water_surface{};
    bool synchronize_to_vertical_refresh{true};
};

struct RenderFrameData final {
    math::Matrix4x4 view_projection{};
    math::Matrix4x4 sky_view_projection{};
    DaylightSettings daylight{};
    math::Float3 camera_world_position{};
    TerrainRenderMode terrain_mode{TerrainRenderMode::solid};
    TerrainMaterialView terrain_material_view{
        TerrainMaterialView::shaded};
    EnvironmentLightingMode environment_lighting_mode{
        EnvironmentLightingMode::image_based};
    float visual_time_seconds{};
    bool terrain_diagnostics_enabled{false};
};

enum class RenderStatus : std::uint8_t {
    presented = 1,
    occluded,
};

struct RendererStats final {
    std::uint64_t presented_frames{};
    std::uint64_t occluded_frames{};
    std::uint64_t resize_count{};
    std::uint32_t frame_context_count{};
    std::uint32_t used_frame_context_mask{};
    std::uint64_t frame_context_acquisitions{};
    std::uint64_t frame_context_reuses{};
    std::uint64_t frame_submissions{};
    std::uint64_t retired_frame_submissions{};
    std::uint64_t blocking_reuse_waits{};
    std::uint64_t full_queue_drains{};
    std::uint64_t upload_allocations{};
    std::uint64_t upload_bytes_written{};
    std::uint64_t upload_high_water_bytes{};
    std::uint64_t descriptor_allocations{};
    std::uint64_t descriptor_high_water_count{};
    std::uint64_t render_graph_compilations{};
    std::uint64_t render_graph_executions{};
    std::uint64_t render_graph_resource_imports{};
    std::uint64_t render_graph_pass_executions{};
    std::uint64_t render_graph_dependencies{};
    std::uint64_t render_graph_transition_barriers{};
    std::uint64_t render_graph_elided_transitions{};
    std::uint64_t pix_static_upload_events{};
    std::uint64_t pix_frame_events{};
    std::uint64_t pix_pass_events{};
    std::uint64_t pix_terrain_events{};
    std::uint64_t pix_textured_cube_events{};
    std::uint64_t pix_water_events{};
    std::uint64_t pix_skybox_events{};
    std::uint64_t pix_tone_map_events{};
    std::uint64_t gpu_timestamp_frequency_hz{};
    std::uint64_t timestamp_query_capacity{};
    std::uint64_t timestamp_query_high_water{};
    std::uint64_t timestamp_queries_written{};
    std::uint64_t timestamp_resolve_batches{};
    std::uint64_t gpu_timing_samples{};
    std::uint64_t gpu_frame_total_ticks{};
    std::uint64_t gpu_frame_min_ticks{};
    std::uint64_t gpu_frame_max_ticks{};
    std::uint64_t gpu_frame_last_ticks{};
    std::uint64_t gpu_terrain_total_ticks{};
    std::uint64_t gpu_terrain_min_ticks{};
    std::uint64_t gpu_terrain_max_ticks{};
    std::uint64_t gpu_terrain_last_ticks{};
    std::uint64_t gpu_textured_cube_total_ticks{};
    std::uint64_t gpu_textured_cube_min_ticks{};
    std::uint64_t gpu_textured_cube_max_ticks{};
    std::uint64_t gpu_textured_cube_last_ticks{};
    std::uint64_t gpu_water_total_ticks{};
    std::uint64_t gpu_water_min_ticks{};
    std::uint64_t gpu_water_max_ticks{};
    std::uint64_t gpu_water_last_ticks{};
    std::uint64_t gpu_skybox_total_ticks{};
    std::uint64_t gpu_skybox_min_ticks{};
    std::uint64_t gpu_skybox_max_ticks{};
    std::uint64_t gpu_skybox_last_ticks{};
    std::uint64_t gpu_tone_map_total_ticks{};
    std::uint64_t gpu_tone_map_min_ticks{};
    std::uint64_t gpu_tone_map_max_ticks{};
    std::uint64_t gpu_tone_map_last_ticks{};
    std::uint64_t cube_draw_calls{};
    std::uint64_t cube_indices{};
    std::uint64_t water_draw_calls{};
    std::uint64_t water_vertices{};
    std::uint64_t skybox_draw_calls{};
    std::uint64_t skybox_indices{};
    std::uint64_t terrain_draw_calls{};
    std::uint64_t terrain_lod0_draw_calls{};
    std::uint64_t terrain_coarse_draw_calls{};
    std::uint64_t terrain_solid_draw_calls{};
    std::uint64_t terrain_wireframe_draw_calls{};
    std::uint64_t terrain_shaded_draw_calls{};
    std::uint64_t terrain_material_weight_draw_calls{};
    std::uint64_t terrain_shading_normal_draw_calls{};
    std::uint64_t terrain_bounds_draw_calls{};
    std::uint64_t terrain_query_marker_draw_calls{};
    std::uint64_t material_sphere_draw_calls{};
    std::uint64_t tone_map_draw_calls{};
    std::uint64_t terrain_chunk_count{};
    std::uint64_t terrain_chunks_tested{};
    std::uint64_t terrain_chunks_visible{};
    std::uint64_t terrain_chunks_culled{};
    std::uint64_t terrain_visible_chunk_min{};
    std::uint64_t terrain_visible_chunk_max{};
    std::uint64_t terrain_visible_chunk_last{};
    std::uint64_t terrain_lod0_chunks_last{};
    std::uint64_t terrain_coarse_chunks_last{};
    std::uint64_t terrain_indices{};
    std::uint64_t terrain_lod0_indices{};
    std::uint64_t terrain_coarse_indices{};
    std::uint64_t terrain_bounds_indices{};
    std::uint64_t terrain_query_marker_indices{};
    std::uint64_t material_sphere_indices{};
    std::uint64_t terrain_vertex_count{};
    std::uint64_t terrain_index_count{};
    std::uint64_t terrain_lod0_index_count{};
    std::uint64_t terrain_coarse_index_count{};
    double terrain_maximum_geometric_error{};
    std::uint64_t terrain_bounds_vertex_count{};
    std::uint64_t terrain_bounds_index_count{};
    std::uint64_t terrain_query_marker_vertex_count{};
    std::uint64_t terrain_query_marker_index_count{};
    std::uint64_t material_sphere_vertex_count{};
    std::uint64_t material_sphere_index_count{};
    std::uint64_t procedural_daylight_frames{};
    std::uint64_t image_based_lighting_frames{};
    std::uint64_t camera_constant_updates{};
    std::uint64_t camera_matrix_changes{};
    std::uint64_t skybox_matrix_changes{};
    std::uint64_t depth_clear_count{};
    std::uint64_t depth_resource_creations{};
    std::uint64_t depth_read_view_creations{};
    std::uint64_t hdr_scene_color_creations{};
    std::uint64_t hdr_scene_color_rtv_creations{};
    std::uint64_t hdr_scene_color_srv_creations{};
    std::uint64_t texture_bindings{};
    std::uint64_t terrain_material_bindings{};
    std::uint64_t static_upload_submissions{};
    std::uint64_t geometry_buffer_creations{};
    std::uint64_t terrain_surface_vertex_payload_bytes{};
    std::uint64_t terrain_surface_index_payload_bytes{};
    std::uint64_t terrain_diagnostic_vertex_payload_bytes{};
    std::uint64_t terrain_diagnostic_index_payload_bytes{};
    std::uint64_t terrain_vertex_resource_bytes{};
    std::uint64_t terrain_index_resource_bytes{};
    std::uint64_t terrain_geometry_resource_bytes{};
    std::uint64_t terrain_geometry_committed_bytes{};
    std::uint64_t checker_texture_creations{};
    std::uint64_t cubemap_texture_creations{};
    std::uint64_t texture_srv_creations{};
    std::uint64_t cubemap_srv_creations{};
    std::uint64_t cubemap_faces_uploaded{};
    std::uint64_t cubemap_mip_levels{};
    std::uint64_t cubemap_subresources_uploaded{};
    std::uint64_t cubemap_source_bytes_uploaded{};
    std::uint64_t terrain_material_texture_array_creations{};
    std::uint64_t terrain_material_srv_creations{};
    std::uint64_t terrain_material_layers{};
    std::uint64_t terrain_material_mip_levels{};
    std::uint64_t terrain_material_subresources_uploaded{};
    std::uint64_t terrain_material_source_bytes_uploaded{};
    std::uint64_t terrain_material_srgb_resources{};
    std::uint64_t environment_texture_creations{};
    std::uint64_t environment_srv_creations{};
    std::uint64_t environment_cubemap_srv_creations{};
    std::uint64_t environment_subresources_uploaded{};
    std::uint64_t environment_source_bytes_uploaded{};
    std::uint64_t environment_hdr_resources{};
    std::uint64_t persistent_texture_descriptors{};
    std::uint64_t cubemap_srgb_resources{};
    std::uint64_t last_submission_fence{};

    [[nodiscard]] friend bool operator==(
        const RendererStats&,
        const RendererStats&) noexcept = default;
};

// Renderer owns environment-pass policy and statistics. Its current private
// backend is Direct3D 12, selected explicitly at the composition root without
// exposing native D3D12, DXGI, WRL, or Win32 types through this header.
// Device and the native window must both outlive Renderer. All methods must be
// called from the thread that owns the native window.
class Renderer final {
public:
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    ~Renderer();

    [[nodiscard]] static core::Result<Renderer> create(
        rhi::d3d12::Device& device,
        const RendererConfig& config);

    [[nodiscard]] core::Result<RenderStatus> render_frame(
        const RenderFrameData& frame_data);
    [[nodiscard]] core::Result<void> resize(RenderExtent extent);
    [[nodiscard]] core::Result<void> shutdown();

    [[nodiscard]] RenderExtent extent() const noexcept;
    [[nodiscard]] const RendererStats& stats() const noexcept;
    [[nodiscard]] bool is_shutdown() const noexcept;

private:
    class Implementation;

    explicit Renderer(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

} // namespace shark::renderer
