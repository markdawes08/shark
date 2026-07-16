#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace shark::rhi::d3d12 {

class Device;

struct PresentationExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend bool operator==(
        const PresentationExtent&,
        const PresentationExtent&) noexcept = default;
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

struct PresentationConfig final {
    void* native_window{};
    PresentationExtent extent{1280, 720};
    ClearColor clear_color{};
    ShaderBytecodeView vertex_shader{};
    ShaderBytecodeView pixel_shader{};
    bool synchronize_to_vertical_refresh{true};
};

struct PresentationFrameData final {
    math::Matrix4x4 view_projection{};
};

enum class PresentStatus : std::uint8_t {
    presented = 1,
    occluded,
};

struct PresentationStats final {
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
    std::uint64_t render_graph_transition_barriers{};
    std::uint64_t cube_draw_calls{};
    std::uint64_t cube_indices{};
    std::uint64_t camera_constant_updates{};
    std::uint64_t camera_matrix_changes{};
    std::uint64_t depth_clear_count{};
    std::uint64_t depth_resource_creations{};
    std::uint64_t texture_bindings{};
    std::uint64_t static_upload_submissions{};
    std::uint64_t geometry_buffer_creations{};
    std::uint64_t checker_texture_creations{};
    std::uint64_t texture_srv_creations{};
    std::uint64_t last_submission_fence{};
};

// Device and the native window must both outlive Presentation. All methods
// must be called from the thread that owns the native window.
class Presentation final {
public:
    Presentation(const Presentation&) = delete;
    Presentation& operator=(const Presentation&) = delete;
    Presentation(Presentation&&) noexcept;
    Presentation& operator=(Presentation&&) noexcept;
    ~Presentation();

    [[nodiscard]] static core::Result<Presentation> create(
        Device& device,
        const PresentationConfig& config);

    [[nodiscard]] core::Result<PresentStatus> present_frame(
        const PresentationFrameData& frame_data);
    [[nodiscard]] core::Result<void> resize(PresentationExtent extent);
    [[nodiscard]] core::Result<void> shutdown();

    [[nodiscard]] PresentationExtent extent() const noexcept;
    [[nodiscard]] const PresentationStats& stats() const noexcept;
    [[nodiscard]] bool is_shutdown() const noexcept;

private:
    class Implementation;

    explicit Presentation(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

} // namespace shark::rhi::d3d12
