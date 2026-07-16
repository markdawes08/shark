#include "device_access.hpp"
#include "cube_scene_data.hpp"
#include "frame_resource_state.hpp"
#include "render_graph_executor.hpp"

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <shark/core/assertion.hpp>
#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/render_graph/render_graph.hpp>
#include <shark/rhi/d3d12/device.hpp>
#include <shark/rhi/d3d12/presentation.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace shark::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT back_buffer_count = 3;
constexpr DXGI_FORMAT back_buffer_format =
    DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DWORD fence_wait_slice_milliseconds = 250;
constexpr std::uint32_t maximum_fence_wait_slices = 120;
constexpr std::size_t upload_bytes_per_frame = 64U * 1024U;
constexpr UINT transient_descriptors_per_frame = 64;
constexpr std::size_t frame_probe_bytes =
    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
constexpr std::size_t camera_matrix_bytes = sizeof(math::Matrix4x4);
constexpr std::size_t frame_probe_offset = camera_matrix_bytes;
constexpr UINT root_camera_constants = 0;
constexpr UINT root_checker_texture = 1;
constexpr render_graph::ExternalResourceId graph_back_buffer_id{1};
constexpr render_graph::ExternalResourceId graph_depth_buffer_id{2};

static_assert(back_buffer_count <= 32);
static_assert(camera_matrix_bytes == 64);

struct FrameProbe final {
    std::uint64_t frame_ordinal{};
    std::uint64_t context_generation{};
    std::uint32_t back_buffer_index{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t reserved{};
};

static_assert(
    frame_probe_offset + sizeof(FrameProbe) <= frame_probe_bytes);

[[nodiscard]] core::Error graphics_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool valid_extent(
    const PresentationExtent extent) noexcept
{
    constexpr std::uint32_t maximum_dimension =
        D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    return extent.width != 0 &&
        extent.height != 0 &&
        extent.width <= maximum_dimension &&
        extent.height <= maximum_dimension;
}

[[nodiscard]] bool valid_clear_color(
    const ClearColor color) noexcept
{
    return std::isfinite(color.red) &&
        std::isfinite(color.green) &&
        std::isfinite(color.blue) &&
        std::isfinite(color.alpha);
}

[[nodiscard]] bool valid_shader_bytecode(
    const ShaderBytecodeView bytecode) noexcept
{
    constexpr std::array<char, 4> container_signature{
        'D',
        'X',
        'B',
        'C',
    };
    return bytecode.data != nullptr &&
        bytecode.size >= container_signature.size() &&
        std::memcmp(
            bytecode.data,
            container_signature.data(),
            container_signature.size()) == 0;
}

[[nodiscard]] bool valid_frame_data(
    const PresentationFrameData& frame_data) noexcept
{
    return math::is_finite(frame_data.view_projection);
}

[[nodiscard]] D3D12_BLEND_DESC opaque_blend_description() noexcept
{
    D3D12_BLEND_DESC description{};
    description.AlphaToCoverageEnable = FALSE;
    description.IndependentBlendEnable = FALSE;
    for (auto& target : description.RenderTarget) {
        target.BlendEnable = FALSE;
        target.LogicOpEnable = FALSE;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_ZERO;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ZERO;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = static_cast<UINT8>(
            D3D12_COLOR_WRITE_ENABLE_ALL);
    }
    return description;
}

[[nodiscard]] D3D12_RASTERIZER_DESC cube_rasterizer_description()
    noexcept
{
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = D3D12_FILL_MODE_SOLID;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.FrontCounterClockwise = FALSE;
    description.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.SlopeScaledDepthBias =
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.DepthClipEnable = TRUE;
    description.MultisampleEnable = FALSE;
    description.AntialiasedLineEnable = FALSE;
    description.ForcedSampleCount = 0;
    description.ConservativeRaster =
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return description;
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC reversed_depth_description()
    noexcept
{
    D3D12_DEPTH_STENCILOP_DESC disabled_stencil{};
    disabled_stencil.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    disabled_stencil.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    disabled_stencil.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    disabled_stencil.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = TRUE;
    description.DepthWriteMask = detail::cube_depth_write_mask;
    description.DepthFunc = detail::cube_depth_comparison;
    description.StencilEnable = FALSE;
    description.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    description.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    description.FrontFace = disabled_stencil;
    description.BackFace = disabled_stencil;
    return description;
}

[[nodiscard]] std::string root_signature_operation(
    ID3DBlob* const diagnostic_blob)
{
    auto operation = std::string{"D3D12SerializeRootSignature"};
    if (diagnostic_blob == nullptr ||
        diagnostic_blob->GetBufferPointer() == nullptr ||
        diagnostic_blob->GetBufferSize() == 0) {
        return operation;
    }

    auto diagnostic_size = diagnostic_blob->GetBufferSize();
    const auto* const diagnostic = static_cast<const char*>(
        diagnostic_blob->GetBufferPointer());
    while (diagnostic_size != 0 &&
           diagnostic[diagnostic_size - 1] == '\0') {
        --diagnostic_size;
    }
    operation.append(": ");
    operation.append(diagnostic, diagnostic_size);
    return operation;
}

[[nodiscard]] core::Error windows_failure(
    const std::string_view operation,
    DWORD error)
{
    if (error == ERROR_SUCCESS) {
        error = ERROR_GEN_FAILURE;
    }
    auto message = std::string{operation};
    message.append(" failed with Windows error ");
    message.append(std::to_string(error));
    return graphics_error(
        core::ErrorCode::operation_failed,
        std::move(message));
}

} // namespace

class Presentation::Implementation final {
public:
    struct FrameContext final {
        detail::FrameResourceState state{
            upload_bytes_per_frame,
            transient_descriptors_per_frame};
        ComPtr<ID3D12CommandAllocator> command_allocator;
        ComPtr<ID3D12Resource> upload_buffer;
        std::byte* mapped_upload{};
        ComPtr<ID3D12Resource> probe_destination;
        ComPtr<ID3D12DescriptorHeap> descriptor_heap;
        std::size_t staged_probe_offset{};
    };

    ~Implementation() noexcept
    {
        if (shutdown_complete) {
            return;
        }

        auto result = shutdown();
        if (!result) {
            core::log_message(
                core::LogLevel::error,
                "gpu.presentation",
                result.error().message());
            release_resources();
            shutdown_complete = true;
        }
    }

    [[nodiscard]] core::Result<void> require_active(
        const std::string_view operation) const
    {
        if (shutdown_complete) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::invalid_state,
                std::string{operation} +
                    " cannot run after presentation shutdown"));
        }
        if (GetCurrentThreadId() != owner_thread) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::invalid_state,
                std::string{operation} +
                    " must run on the native window's owning thread"));
        }
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> wait_for_fence(
        const UINT64 value)
    {
        auto completed_value = fence->GetCompletedValue();
        if (completed_value == UINT64_MAX) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Fence::GetCompletedValue",
                    DXGI_ERROR_DEVICE_REMOVED));
        }
        if (completed_value >= value) {
            return core::Result<void>::success();
        }

        const auto event_result = fence->SetEventOnCompletion(
            value,
            fence_event);
        if (FAILED(event_result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Fence::SetEventOnCompletion",
                    event_result));
        }

        for (std::uint32_t attempt = 0;
             attempt < maximum_fence_wait_slices;
             ++attempt) {
            const auto wait_result = WaitForSingleObject(
                fence_event,
                fence_wait_slice_milliseconds);
            if (wait_result == WAIT_OBJECT_0) {
                completed_value = fence->GetCompletedValue();
                if (completed_value == UINT64_MAX) {
                    return core::Result<void>::failure(
                        detail::DeviceAccess::graphics_failure(
                            *owner_device,
                            "ID3D12Fence::GetCompletedValue",
                            DXGI_ERROR_DEVICE_REMOVED));
                }
                if (completed_value >= value) {
                    return core::Result<void>::success();
                }
                continue;
            }
            if (wait_result == WAIT_FAILED) {
                return core::Result<void>::failure(windows_failure(
                    "WaitForSingleObject(presentation fence)",
                    GetLastError()));
            }
            if (wait_result != WAIT_TIMEOUT) {
                return core::Result<void>::failure(graphics_error(
                    core::ErrorCode::operation_failed,
                    "The presentation fence wait returned an unexpected "
                    "status"));
            }

            const auto removal_reason = native_device->GetDeviceRemovedReason();
            if (FAILED(removal_reason)) {
                return core::Result<void>::failure(
                    detail::DeviceAccess::graphics_failure(
                        *owner_device,
                        "Presentation fence wait",
                        removal_reason));
            }
        }

        return core::Result<void>::failure(
            detail::DeviceAccess::graphics_failure(
                *owner_device,
                "Presentation fence wait (30 second timeout)",
                HRESULT_FROM_WIN32(WAIT_TIMEOUT)));
    }

    [[nodiscard]] core::Result<UINT64> completed_fence_value()
    {
        const auto completed_value = fence->GetCompletedValue();
        if (completed_value == UINT64_MAX) {
            return core::Result<UINT64>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Fence::GetCompletedValue",
                    DXGI_ERROR_DEVICE_REMOVED));
        }
        return core::Result<UINT64>::success(completed_value);
    }

    [[nodiscard]] core::Result<UINT64> signal_queue(
        const std::string_view operation)
    {
        auto value_result = fence_timeline.issue();
        if (!value_result) {
            return core::Result<UINT64>::failure(
                std::move(value_result).error());
        }
        const auto value = value_result.value();
        const auto signal_result = command_queue->Signal(
            fence.Get(),
            value);
        if (FAILED(signal_result)) {
            return core::Result<UINT64>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    operation,
                    signal_result));
        }
        return core::Result<UINT64>::success(value);
    }

    [[nodiscard]] core::Result<void> retire_completed_contexts(
        const UINT64 completed_value)
    {
        for (auto& context : frame_contexts) {
            if (context.state.active()) {
                context.state.discard_active_after_queue_drain();
                continue;
            }
            auto retire_result = context.state.retire(completed_value);
            if (!retire_result) {
                return core::Result<void>::failure(
                    std::move(retire_result).error());
            }
            if (retire_result.value()) {
                ++statistics.retired_frame_submissions;
            }
        }
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> drain_queue()
    {
        auto value_result = signal_queue(
            "ID3D12CommandQueue::Signal(queue drain)");
        if (!value_result) {
            return core::Result<void>::failure(
                std::move(value_result).error());
        }
        const auto value = value_result.value();
        ++statistics.full_queue_drains;
        auto wait_result = wait_for_fence(value);
        if (!wait_result) {
            return wait_result;
        }
        return retire_completed_contexts(value);
    }

    [[nodiscard]] core::Result<FrameContext*> begin_frame(
        const UINT back_buffer_index)
    {
        auto& context = frame_contexts[back_buffer_index];
        auto completed_result = completed_fence_value();
        if (!completed_result) {
            return core::Result<FrameContext*>::failure(
                std::move(completed_result).error());
        }
        auto completed_value = completed_result.value();
        const auto required_value =
            context.state.required_wait_fence_value(completed_value);
        if (required_value != 0) {
            ++statistics.blocking_reuse_waits;
            auto wait_result = wait_for_fence(required_value);
            if (!wait_result) {
                return core::Result<FrameContext*>::failure(
                    std::move(wait_result).error());
            }
            completed_value = required_value;
        }

        auto begin_result = context.state.begin(completed_value);
        if (!begin_result) {
            return core::Result<FrameContext*>::failure(
                std::move(begin_result).error());
        }
        if (begin_result.value()) {
            ++statistics.retired_frame_submissions;
        }

        ++statistics.frame_context_acquisitions;
        if (context.state.generation() > 1) {
            ++statistics.frame_context_reuses;
        }
        statistics.used_frame_context_mask |=
            std::uint32_t{1} << back_buffer_index;
        return core::Result<FrameContext*>::success(&context);
    }

    [[nodiscard]] core::Result<void> stage_frame_data(
        FrameContext& context,
        const UINT back_buffer_index,
        const PresentationFrameData& frame_data)
    {
        auto upload_result = context.state.allocate_upload(
            frame_probe_bytes,
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (!upload_result) {
            return core::Result<void>::failure(
                std::move(upload_result).error());
        }
        auto descriptor_result = context.state.allocate_descriptors(1);
        if (!descriptor_result) {
            return core::Result<void>::failure(
                std::move(descriptor_result).error());
        }

        const auto upload = upload_result.value();
        const auto descriptor = descriptor_result.value();
        auto* const destination = context.mapped_upload + upload.offset;
        std::memset(destination, 0, upload.size);
        std::memcpy(
            destination,
            &frame_data.view_projection,
            camera_matrix_bytes);
        const FrameProbe probe{
            statistics.frame_context_acquisitions,
            context.state.generation(),
            back_buffer_index,
            current_extent.width,
            current_extent.height,
            0,
        };
        std::memcpy(
            destination + frame_probe_offset,
            &probe,
            sizeof(probe));

        auto descriptor_handle =
            context.descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        descriptor_handle.ptr += descriptor.offset *
            static_cast<std::size_t>(transient_descriptor_increment);
        const D3D12_CONSTANT_BUFFER_VIEW_DESC view{
            .BufferLocation =
                context.upload_buffer->GetGPUVirtualAddress() +
                upload.offset,
            .SizeInBytes = static_cast<UINT>(upload.size),
        };
        native_device->CreateConstantBufferView(&view, descriptor_handle);
        context.staged_probe_offset = upload.offset;

        ++statistics.camera_constant_updates;
        if (!has_last_camera_matrix ||
            std::memcmp(
                last_camera_matrix.data(),
                &frame_data.view_projection,
                camera_matrix_bytes) != 0) {
            ++statistics.camera_matrix_changes;
            std::memcpy(
                last_camera_matrix.data(),
                &frame_data.view_projection,
                camera_matrix_bytes);
            has_last_camera_matrix = true;
        }
        ++statistics.upload_allocations;
        statistics.upload_bytes_written += upload.size;
        statistics.upload_high_water_bytes = std::max(
            statistics.upload_high_water_bytes,
            static_cast<std::uint64_t>(
                context.state.upload_high_watermark()));
        ++statistics.descriptor_allocations;
        statistics.descriptor_high_water_count = std::max(
            statistics.descriptor_high_water_count,
            static_cast<std::uint64_t>(
                context.state.descriptor_high_watermark()));
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> record_textured_cube_pass(
        const render_graph::PassContext& pass_context,
        const render_graph::ResourceHandle back_buffer_resource,
        const render_graph::ResourceHandle depth_buffer_resource,
        FrameContext& context,
        const UINT back_buffer_index)
    {
        auto back_buffer_access =
            pass_context.write(back_buffer_resource);
        if (!back_buffer_access) {
            return core::Result<void>::failure(
                std::move(back_buffer_access).error());
        }
        auto depth_buffer_access =
            pass_context.write(depth_buffer_resource);
        if (!depth_buffer_access) {
            return core::Result<void>::failure(
                std::move(depth_buffer_access).error());
        }
        if (back_buffer_access.value() != graph_back_buffer_id ||
            depth_buffer_access.value() != graph_depth_buffer_id) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::invalid_state,
                "The textured-cube pass resolved unexpected graph "
                "resource bindings"));
        }

        auto render_target =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        render_target.ptr += static_cast<SIZE_T>(back_buffer_index) *
            static_cast<SIZE_T>(rtv_descriptor_increment);
        const auto depth_stencil =
            dsv_heap->GetCPUDescriptorHandleForHeapStart();
        command_list->OMSetRenderTargets(
            1,
            &render_target,
            FALSE,
            &depth_stencil);
        const std::array<float, 4> frame_clear_color{
            clear_color.red,
            clear_color.green,
            clear_color.blue,
            clear_color.alpha,
        };
        command_list->ClearRenderTargetView(
            render_target,
            frame_clear_color.data(),
            0,
            nullptr);
        command_list->ClearDepthStencilView(
            depth_stencil,
            D3D12_CLEAR_FLAG_DEPTH,
            detail::cube_depth_clear_value,
            0,
            0,
            nullptr);
        ++statistics.depth_clear_count;

        const D3D12_VIEWPORT viewport{
            0.0F,
            0.0F,
            static_cast<float>(current_extent.width),
            static_cast<float>(current_extent.height),
            0.0F,
            1.0F,
        };
        const D3D12_RECT scissor_rectangle{
            0,
            0,
            static_cast<LONG>(current_extent.width),
            static_cast<LONG>(current_extent.height),
        };
        command_list->SetGraphicsRootSignature(
            cube_root_signature.Get());
        ID3D12DescriptorHeap* descriptor_heaps[]{
            checker_descriptor_heap.Get(),
        };
        command_list->SetDescriptorHeaps(
            static_cast<UINT>(std::size(descriptor_heaps)),
            descriptor_heaps);
        command_list->SetGraphicsRootConstantBufferView(
            root_camera_constants,
            context.upload_buffer->GetGPUVirtualAddress() +
                context.staged_probe_offset);
        command_list->SetGraphicsRootDescriptorTable(
            root_checker_texture,
            checker_descriptor_heap->GetGPUDescriptorHandleForHeapStart());
        ++statistics.texture_bindings;
        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(
            1,
            &scissor_rectangle);
        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetVertexBuffers(
            0,
            1,
            &vertex_buffer_view);
        command_list->IASetIndexBuffer(&index_buffer_view);
        command_list->DrawIndexedInstanced(
            static_cast<UINT>(detail::cube_indices.size()),
            1,
            0,
            0,
            0);

        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> submit_frame(
        FrameContext& context)
    {
        auto signal_result = signal_queue(
            "ID3D12CommandQueue::Signal(frame submission)");
        if (!signal_result) {
            return core::Result<void>::failure(
                std::move(signal_result).error());
        }
        const auto value = signal_result.value();
        auto submit_result = context.state.submit(value);
        if (!submit_result) {
            return submit_result;
        }
        ++statistics.frame_submissions;
        statistics.last_submission_fence = value;
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> acquire_back_buffers()
    {
        const auto descriptor_start =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT index = 0; index < back_buffer_count; ++index) {
            ComPtr<ID3D12Resource> buffer;
            const auto buffer_result = swap_chain->GetBuffer(
                index,
                IID_PPV_ARGS(&buffer));
            if (FAILED(buffer_result)) {
                return core::Result<void>::failure(
                    detail::DeviceAccess::graphics_failure(
                        *owner_device,
                        "IDXGISwapChain::GetBuffer",
                        buffer_result));
            }

            const auto name = std::wstring{L"Shark Back Buffer "} +
                std::to_wstring(index);
            const auto name_result = buffer->SetName(name.c_str());
            if (FAILED(name_result)) {
                return core::Result<void>::failure(
                    detail::DeviceAccess::graphics_failure(
                        *owner_device,
                        "ID3D12Object::SetName(back buffer)",
                        name_result));
            }

            auto descriptor = descriptor_start;
            descriptor.ptr += static_cast<SIZE_T>(index) *
                static_cast<SIZE_T>(rtv_descriptor_increment);
            native_device->CreateRenderTargetView(
                buffer.Get(),
                nullptr,
                descriptor);
            back_buffers[index] = std::move(buffer);
        }
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> create_depth_buffer(
        const PresentationExtent extent)
    {
        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty =
            D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference =
            D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Alignment = 0;
        description.Width = extent.width;
        description.Height = extent.height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = detail::cube_depth_format;
        description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = detail::cube_depth_resource_flags;

        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = detail::cube_depth_format;
        clear_value.DepthStencil.Depth = detail::cube_depth_clear_value;
        clear_value.DepthStencil.Stencil = 0;

        const auto result = native_device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            detail::cube_depth_resource_state,
            &clear_value,
            IID_PPV_ARGS(&depth_buffer));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateCommittedResource(depth buffer)",
                    result));
        }
        const auto name_result = depth_buffer->SetName(
            L"Shark Reversed-Z Depth Buffer");
        if (FAILED(name_result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(depth buffer)",
                    name_result));
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = detail::cube_depth_format;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        view.Flags = D3D12_DSV_FLAG_NONE;
        view.Texture2D.MipSlice = 0;
        native_device->CreateDepthStencilView(
            depth_buffer.Get(),
            &view,
            dsv_heap->GetCPUDescriptorHandleForHeapStart());
        ++statistics.depth_resource_creations;
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> create_static_cube_resources()
    {
        constexpr UINT64 vertex_bytes = sizeof(detail::cube_vertices);
        constexpr UINT64 index_bytes = sizeof(detail::cube_indices);
        constexpr UINT64 index_upload_offset =
            (vertex_bytes + 3U) & ~UINT64{3U};
        constexpr UINT64 texture_upload_base =
            (index_upload_offset + index_bytes +
             D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1U) &
            ~UINT64{D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1U};

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        default_heap.CreationNodeMask = 1;
        default_heap.VisibleNodeMask = 1;

        auto buffer_description = [](const UINT64 size) {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            description.Alignment = 0;
            description.Width = size;
            description.Height = 1;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_UNKNOWN;
            description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
            description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            description.Flags = D3D12_RESOURCE_FLAG_NONE;
            return description;
        };

        const auto vertex_description = buffer_description(vertex_bytes);
        auto result = native_device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &vertex_description,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&vertex_buffer));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateCommittedResource(cube vertices)",
                    result));
        }
        result = vertex_buffer->SetName(L"Shark Cube Vertex Buffer");
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(cube vertices)",
                    result));
        }
        ++statistics.geometry_buffer_creations;

        const auto index_description = buffer_description(index_bytes);
        result = native_device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &index_description,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&index_buffer));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateCommittedResource(cube indices)",
                    result));
        }
        result = index_buffer->SetName(L"Shark Cube Index Buffer");
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(cube indices)",
                    result));
        }
        ++statistics.geometry_buffer_creations;

        D3D12_RESOURCE_DESC texture_description{};
        texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_description.Alignment = 0;
        texture_description.Width = detail::checker_width;
        texture_description.Height = detail::checker_height;
        texture_description.DepthOrArraySize = 1;
        texture_description.MipLevels = detail::checker_mip_levels;
        texture_description.Format = detail::checker_format;
        texture_description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
        texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texture_description.Flags = D3D12_RESOURCE_FLAG_NONE;
        result = native_device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &texture_description,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&checker_texture));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateCommittedResource(checker texture)",
                    result));
        }
        result = checker_texture->SetName(L"Shark Procedural Checker Texture");
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(checker texture)",
                    result));
        }
        ++statistics.checker_texture_creations;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT texture_footprint{};
        UINT texture_rows = 0;
        UINT64 texture_row_bytes = 0;
        UINT64 upload_bytes = 0;
        native_device->GetCopyableFootprints(
            &texture_description,
            0,
            1,
            texture_upload_base,
            &texture_footprint,
            &texture_rows,
            &texture_row_bytes,
            &upload_bytes);
        upload_bytes += texture_footprint.Offset;

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap.CreationNodeMask = 1;
        upload_heap.VisibleNodeMask = 1;
        const auto upload_description = buffer_description(upload_bytes);
        ComPtr<ID3D12Resource> upload_buffer;
        result = native_device->CreateCommittedResource(
            &upload_heap,
            D3D12_HEAP_FLAG_NONE,
            &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload_buffer));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateCommittedResource(static upload)",
                    result));
        }
        result = upload_buffer->SetName(L"Shark Cube Static Upload Buffer");
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(static upload)",
                    result));
        }

        const D3D12_RANGE no_cpu_reads{0, 0};
        void* mapped_data = nullptr;
        result = upload_buffer->Map(0, &no_cpu_reads, &mapped_data);
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Resource::Map(static upload)",
                    result));
        }
        auto* const upload_data = static_cast<std::byte*>(mapped_data);
        std::memcpy(
            upload_data,
            detail::cube_vertices.data(),
            vertex_bytes);
        std::memcpy(
            upload_data + index_upload_offset,
            detail::cube_indices.data(),
            index_bytes);
        constexpr UINT64 checker_source_row_bytes =
            detail::checker_width * 4U;
        static_assert(
            sizeof(detail::checker_pixels) ==
            checker_source_row_bytes * detail::checker_height);
        for (UINT row = 0; row < texture_rows; ++row) {
            std::memcpy(
                upload_data + texture_footprint.Offset +
                    static_cast<UINT64>(row) *
                        texture_footprint.Footprint.RowPitch,
                detail::checker_pixels.data() +
                    static_cast<std::size_t>(row) *
                        checker_source_row_bytes,
                static_cast<std::size_t>(texture_row_bytes));
        }
        upload_buffer->Unmap(0, nullptr);

        D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_description{};
        descriptor_heap_description.Type =
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptor_heap_description.NumDescriptors = 1;
        descriptor_heap_description.Flags =
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        descriptor_heap_description.NodeMask = 0;
        result = native_device->CreateDescriptorHeap(
            &descriptor_heap_description,
            IID_PPV_ARGS(&checker_descriptor_heap));
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Device::CreateDescriptorHeap(checker SRV)",
                    result));
        }
        result = checker_descriptor_heap->SetName(
            L"Shark Persistent Checker Descriptor Heap");
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12Object::SetName(checker descriptor heap)",
                    result));
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view{};
        shader_resource_view.Format = detail::checker_format;
        shader_resource_view.ViewDimension =
            D3D12_SRV_DIMENSION_TEXTURE2D;
        shader_resource_view.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shader_resource_view.Texture2D.MostDetailedMip = 0;
        shader_resource_view.Texture2D.MipLevels =
            detail::checker_mip_levels;
        shader_resource_view.Texture2D.PlaneSlice = 0;
        shader_resource_view.Texture2D.ResourceMinLODClamp = 0.0F;
        native_device->CreateShaderResourceView(
            checker_texture.Get(),
            &shader_resource_view,
            checker_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
        ++statistics.texture_srv_creations;

        auto& upload_allocator = frame_contexts[0].command_allocator;
        result = upload_allocator->Reset();
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12CommandAllocator::Reset(static upload)",
                    result));
        }
        result = command_list->Reset(upload_allocator.Get(), nullptr);
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12GraphicsCommandList::Reset(static upload)",
                    result));
        }

        command_list->CopyBufferRegion(
            vertex_buffer.Get(),
            0,
            upload_buffer.Get(),
            0,
            vertex_bytes);
        command_list->CopyBufferRegion(
            index_buffer.Get(),
            0,
            upload_buffer.Get(),
            index_upload_offset,
            index_bytes);
        D3D12_TEXTURE_COPY_LOCATION texture_destination{};
        texture_destination.pResource = checker_texture.Get();
        texture_destination.Type =
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        texture_destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION texture_source{};
        texture_source.pResource = upload_buffer.Get();
        texture_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        texture_source.PlacedFootprint = texture_footprint;
        command_list->CopyTextureRegion(
            &texture_destination,
            0,
            0,
            0,
            &texture_source,
            nullptr);

        // G-006 manages frame color/depth attachments. These one-time
        // initialization transitions remain with the static upload batch.
        std::array<D3D12_RESOURCE_BARRIER, 3> barriers{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = vertex_buffer.Get();
        barriers[0].Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore =
            D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter =
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barriers[1] = barriers[0];
        barriers[1].Transition.pResource = index_buffer.Get();
        barriers[1].Transition.StateAfter =
            D3D12_RESOURCE_STATE_INDEX_BUFFER;
        barriers[2] = barriers[0];
        barriers[2].Transition.pResource = checker_texture.Get();
        barriers[2].Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        command_list->ResourceBarrier(
            static_cast<UINT>(barriers.size()),
            barriers.data());

        result = command_list->Close();
        if (FAILED(result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "ID3D12GraphicsCommandList::Close(static upload)",
                    result));
        }
        ID3D12CommandList* command_lists[]{command_list.Get()};
        command_queue->ExecuteCommandLists(1, command_lists);
        ++statistics.static_upload_submissions;
        auto fence_result = signal_queue(
            "ID3D12CommandQueue::Signal(static cube upload)");
        if (!fence_result) {
            return core::Result<void>::failure(
                std::move(fence_result).error());
        }
        auto wait_result = wait_for_fence(fence_result.value());
        if (!wait_result) {
            return wait_result;
        }

        vertex_buffer_view.BufferLocation =
            vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.SizeInBytes =
            static_cast<UINT>(vertex_bytes);
        vertex_buffer_view.StrideInBytes = detail::cube_vertex_stride;
        index_buffer_view.BufferLocation =
            index_buffer->GetGPUVirtualAddress();
        index_buffer_view.SizeInBytes = static_cast<UINT>(index_bytes);
        index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> verify_swap_chain_extent(
        const PresentationExtent expected_extent)
    {
        DXGI_SWAP_CHAIN_DESC1 description{};
        const auto description_result = swap_chain->GetDesc1(&description);
        if (FAILED(description_result)) {
            return core::Result<void>::failure(
                detail::DeviceAccess::graphics_failure(
                    *owner_device,
                    "IDXGISwapChain1::GetDesc1",
                    description_result));
        }
        if (description.Width != expected_extent.width ||
            description.Height != expected_extent.height) {
            auto message = std::string{
                "DXGI reported swap-chain extent "};
            message.append(std::to_string(description.Width));
            message.push_back('x');
            message.append(std::to_string(description.Height));
            message.append(" after Shark requested ");
            message.append(std::to_string(expected_extent.width));
            message.push_back('x');
            message.append(std::to_string(expected_extent.height));
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::operation_failed,
                std::move(message)));
        }
        return core::Result<void>::success();
    }

    void release_back_buffers() noexcept
    {
        depth_buffer.Reset();
        for (auto& buffer : back_buffers) {
            buffer.Reset();
        }
    }

    void release_resources() noexcept
    {
        release_back_buffers();
        swap_chain.Reset();
        command_list.Reset();
        cube_pipeline.Reset();
        cube_root_signature.Reset();
        checker_descriptor_heap.Reset();
        checker_texture.Reset();
        index_buffer.Reset();
        vertex_buffer.Reset();
        for (auto& context : frame_contexts) {
            if (context.upload_buffer != nullptr &&
                context.mapped_upload != nullptr) {
                context.upload_buffer->Unmap(0, nullptr);
                context.mapped_upload = nullptr;
            }
            context.descriptor_heap.Reset();
            context.probe_destination.Reset();
            context.upload_buffer.Reset();
            context.command_allocator.Reset();
        }
        dsv_heap.Reset();
        rtv_heap.Reset();
        command_queue.Reset();
        fence.Reset();
        if (fence_event != nullptr) {
            static_cast<void>(CloseHandle(fence_event));
            fence_event = nullptr;
        }
    }

    [[nodiscard]] core::Result<void> shutdown()
    {
        if (shutdown_complete) {
            return core::Result<void>::success();
        }
        if (GetCurrentThreadId() != owner_thread) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::invalid_state,
                "Presentation shutdown must run on the native window's "
                "owning thread"));
        }

        core::Result<void> idle_result =
            command_queue != nullptr &&
                fence != nullptr &&
                fence_event != nullptr
            ? drain_queue()
            : core::Result<void>::success();
        const auto has_unretired_submissions = idle_result &&
            statistics.retired_frame_submissions !=
                statistics.frame_submissions;
        release_resources();
        shutdown_complete = true;

        if (!idle_result) {
            return core::Result<void>::failure(
                std::move(idle_result).error());
        }
        if (has_unretired_submissions) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::invalid_state,
                "Presentation shutdown found unretired frame "
                "submissions after the queue drain"));
        }
        return core::Result<void>::success();
    }

    Device* owner_device{};
    ID3D12Device* native_device{};
    DWORD owner_thread{};
    PresentationExtent current_extent{};
    ClearColor clear_color{};
    bool synchronize_to_vertical_refresh{};
    bool shutdown_complete{true};
    PresentationStats statistics{};
    ComPtr<ID3D12CommandQueue> command_queue;
    std::array<FrameContext, back_buffer_count> frame_contexts;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<ID3D12PipelineState> cube_pipeline;
    ComPtr<ID3D12RootSignature> cube_root_signature;
    ComPtr<ID3D12Resource> vertex_buffer;
    ComPtr<ID3D12Resource> index_buffer;
    ComPtr<ID3D12Resource> checker_texture;
    ComPtr<ID3D12Resource> depth_buffer;
    ComPtr<ID3D12DescriptorHeap> checker_descriptor_heap;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    ComPtr<IDXGISwapChain4> swap_chain;
    std::array<ComPtr<ID3D12Resource>, back_buffer_count> back_buffers;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event{};
    UINT rtv_descriptor_increment{};
    UINT transient_descriptor_increment{};
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
    std::array<float, 16> last_camera_matrix{};
    bool has_last_camera_matrix{};
    detail::FenceTimeline fence_timeline;
};

Presentation::Presentation(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation))
{
}

Presentation::Presentation(Presentation&&) noexcept = default;
Presentation& Presentation::operator=(Presentation&&) noexcept = default;
Presentation::~Presentation() = default;

core::Result<Presentation> Presentation::create(
    Device& device,
    const PresentationConfig& config)
{
    if (config.native_window == nullptr) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation requires a native window"));
    }
    if (!valid_extent(config.extent)) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation extent must be nonzero and within D3D12 limits"));
    }
    if (!valid_clear_color(config.clear_color)) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation clear color components must be finite"));
    }
    if (!valid_shader_bytecode(config.vertex_shader)) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation vertex shader bytecode is missing a DXIL "
            "container signature"));
    }
    if (!valid_shader_bytecode(config.pixel_shader)) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation pixel shader bytecode is missing a DXIL "
            "container signature"));
    }

    const auto native_window = static_cast<HWND>(config.native_window);
    if (IsWindow(native_window) == FALSE) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation received an invalid native window"));
    }
    const auto window_thread = GetWindowThreadProcessId(
        native_window,
        nullptr);
    if (window_thread == 0 || window_thread != GetCurrentThreadId()) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "Presentation must be created on the native window's owning "
            "thread"));
    }

    const auto native = detail::DeviceAccess::native_context(device);
    if (native.device == nullptr || native.factory == nullptr) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "Presentation requires an initialized D3D12 Device"));
    }

    auto implementation = std::make_unique<Implementation>();
    implementation->owner_device = &device;
    implementation->native_device = native.device;
    implementation->owner_thread = window_thread;
    implementation->current_extent = config.extent;
    implementation->clear_color = config.clear_color;
    implementation->synchronize_to_vertical_refresh =
        config.synchronize_to_vertical_refresh;
    implementation->shutdown_complete = false;

    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_description.NodeMask = 0;
    auto result = native.device->CreateCommandQueue(
        &queue_description,
        IID_PPV_ARGS(&implementation->command_queue));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateCommandQueue",
                result));
    }
    result = implementation->command_queue->SetName(
        L"Shark Direct Presentation Queue");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(command queue)",
                result));
    }

    DXGI_SWAP_CHAIN_DESC1 swap_chain_description{};
    swap_chain_description.Width = config.extent.width;
    swap_chain_description.Height = config.extent.height;
    swap_chain_description.Format = back_buffer_format;
    swap_chain_description.Stereo = FALSE;
    swap_chain_description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
    swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_description.BufferCount = back_buffer_count;
    swap_chain_description.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_chain_description.Flags = 0;

    ComPtr<IDXGISwapChain1> swap_chain_one;
    result = native.factory->CreateSwapChainForHwnd(
        implementation->command_queue.Get(),
        native_window,
        &swap_chain_description,
        nullptr,
        nullptr,
        &swap_chain_one);
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "IDXGIFactory::CreateSwapChainForHwnd",
                result));
    }
    result = swap_chain_one.As(&implementation->swap_chain);
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "QueryInterface(IDXGISwapChain4)",
                result));
    }
    auto extent_result = implementation->verify_swap_chain_extent(
        config.extent);
    if (!extent_result) {
        return core::Result<Presentation>::failure(
            std::move(extent_result).error());
    }
    result = native.factory->MakeWindowAssociation(
        native_window,
        DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "IDXGIFactory::MakeWindowAssociation",
                result));
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = back_buffer_count;
    heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_description.NodeMask = 0;
    result = native.device->CreateDescriptorHeap(
        &heap_description,
        IID_PPV_ARGS(&implementation->rtv_heap));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateDescriptorHeap(RTV)",
                result));
    }
    result = implementation->rtv_heap->SetName(
        L"Shark Presentation RTV Heap");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(RTV heap)",
                result));
    }
    implementation->rtv_descriptor_increment =
        native.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_description.NumDescriptors = 1;
    result = native.device->CreateDescriptorHeap(
        &heap_description,
        IID_PPV_ARGS(&implementation->dsv_heap));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateDescriptorHeap(DSV)",
                result));
    }
    result = implementation->dsv_heap->SetName(
        L"Shark Presentation DSV Heap");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(DSV heap)",
                result));
    }

    for (UINT index = 0; index < back_buffer_count; ++index) {
        auto& context = implementation->frame_contexts[index];
        result = native.device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&context.command_allocator));
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Device::CreateCommandAllocator(frame)",
                    result));
        }
        const auto name = std::wstring{L"Shark Frame Allocator "} +
            std::to_wstring(index);
        result = context.command_allocator->SetName(name.c_str());
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Object::SetName(frame allocator)",
                    result));
        }
    }

    D3D12_DESCRIPTOR_RANGE checker_range{};
    checker_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    checker_range.NumDescriptors = 1;
    checker_range.BaseShaderRegister = 0;
    checker_range.RegisterSpace = 0;
    checker_range.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 2> root_parameters{};
    root_parameters[root_camera_constants].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[root_camera_constants].Descriptor.ShaderRegister = 0;
    root_parameters[root_camera_constants].Descriptor.RegisterSpace = 0;
    root_parameters[root_camera_constants].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[root_checker_texture].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[root_checker_texture].DescriptorTable.NumDescriptorRanges =
        1;
    root_parameters[root_checker_texture].DescriptorTable.pDescriptorRanges =
        &checker_range;
    root_parameters[root_checker_texture].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC checker_sampler{};
    checker_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    checker_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    checker_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    checker_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    checker_sampler.MipLODBias = 0.0F;
    checker_sampler.MaxAnisotropy = 1;
    checker_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    checker_sampler.BorderColor =
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    checker_sampler.MinLOD = 0.0F;
    checker_sampler.MaxLOD = D3D12_FLOAT32_MAX;
    checker_sampler.ShaderRegister = 0;
    checker_sampler.RegisterSpace = 0;
    checker_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC root_signature_description{};
    root_signature_description.NumParameters =
        static_cast<UINT>(root_parameters.size());
    root_signature_description.pParameters = root_parameters.data();
    root_signature_description.NumStaticSamplers = 1;
    root_signature_description.pStaticSamplers = &checker_sampler;
    root_signature_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized_root_signature;
    ComPtr<ID3DBlob> root_signature_diagnostics;
    result = D3D12SerializeRootSignature(
        &root_signature_description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized_root_signature,
        &root_signature_diagnostics);
    if (FAILED(result)) {
        const auto operation = root_signature_operation(
            root_signature_diagnostics.Get());
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                operation,
                result));
    }

    result = native.device->CreateRootSignature(
        0,
        serialized_root_signature->GetBufferPointer(),
        serialized_root_signature->GetBufferSize(),
        IID_PPV_ARGS(&implementation->cube_root_signature));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateRootSignature(cube)",
                result));
    }
    result = implementation->cube_root_signature->SetName(
        L"Shark Textured Cube Root Signature");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(cube root signature)",
                result));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_description{};
    pipeline_description.pRootSignature =
        implementation->cube_root_signature.Get();
    pipeline_description.VS = D3D12_SHADER_BYTECODE{
        config.vertex_shader.data,
        config.vertex_shader.size,
    };
    pipeline_description.PS = D3D12_SHADER_BYTECODE{
        config.pixel_shader.data,
        config.pixel_shader.size,
    };
    pipeline_description.DS = D3D12_SHADER_BYTECODE{};
    pipeline_description.HS = D3D12_SHADER_BYTECODE{};
    pipeline_description.GS = D3D12_SHADER_BYTECODE{};
    pipeline_description.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
    pipeline_description.BlendState = opaque_blend_description();
    pipeline_description.SampleMask = std::numeric_limits<UINT>::max();
    pipeline_description.RasterizerState =
        cube_rasterizer_description();
    pipeline_description.DepthStencilState =
        reversed_depth_description();
    pipeline_description.InputLayout = detail::cube_input_layout;
    pipeline_description.IBStripCutValue =
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    pipeline_description.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_description.NumRenderTargets = 1;
    pipeline_description.RTVFormats[0] = back_buffer_format;
    pipeline_description.DSVFormat = detail::cube_depth_format;
    pipeline_description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
    pipeline_description.NodeMask = 0;
    pipeline_description.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
    pipeline_description.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    result = native.device->CreateGraphicsPipelineState(
        &pipeline_description,
        IID_PPV_ARGS(&implementation->cube_pipeline));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateGraphicsPipelineState(cube)",
                result));
    }
    result = implementation->cube_pipeline->SetName(
        L"Shark Textured Cube Pipeline");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(cube pipeline)",
                result));
    }

    result = native.device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        implementation->frame_contexts[0].command_allocator.Get(),
        implementation->cube_pipeline.Get(),
        IID_PPV_ARGS(&implementation->command_list));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateCommandList",
                result));
    }
    result = implementation->command_list->SetName(
        L"Shark Textured Cube Command List");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(command list)",
                result));
    }
    result = implementation->command_list->Close();
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12GraphicsCommandList::Close(initial)",
                result));
    }

    result = native.device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&implementation->fence));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateFence",
                result));
    }
    result = implementation->fence->SetName(
        L"Shark Direct Queue Timeline Fence");
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Object::SetName(presentation fence)",
                result));
    }

    implementation->fence_event = CreateEventExW(
        nullptr,
        nullptr,
        0,
        EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (implementation->fence_event == nullptr) {
        return core::Result<Presentation>::failure(windows_failure(
            "CreateEventExW(presentation fence)",
            GetLastError()));
    }

    implementation->transient_descriptor_increment =
        native.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    implementation->statistics.frame_context_count = back_buffer_count;

    D3D12_HEAP_PROPERTIES upload_heap_properties{};
    upload_heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_heap_properties.CPUPageProperty =
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    upload_heap_properties.MemoryPoolPreference =
        D3D12_MEMORY_POOL_UNKNOWN;
    upload_heap_properties.CreationNodeMask = 1;
    upload_heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC upload_description{};
    upload_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_description.Alignment = 0;
    upload_description.Width = upload_bytes_per_frame;
    upload_description.Height = 1;
    upload_description.DepthOrArraySize = 1;
    upload_description.MipLevels = 1;
    upload_description.Format = DXGI_FORMAT_UNKNOWN;
    upload_description.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
    upload_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_description.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES default_heap_properties{};
    default_heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap_properties.CPUPageProperty =
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    default_heap_properties.MemoryPoolPreference =
        D3D12_MEMORY_POOL_UNKNOWN;
    default_heap_properties.CreationNodeMask = 1;
    default_heap_properties.VisibleNodeMask = 1;
    auto probe_description = upload_description;
    probe_description.Width = frame_probe_bytes;

    for (UINT index = 0; index < back_buffer_count; ++index) {
        auto& context = implementation->frame_contexts[index];
        result = native.device->CreateCommittedResource(
            &upload_heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&context.upload_buffer));
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Device::CreateCommittedResource(frame upload)",
                    result));
        }
        auto name = std::wstring{L"Shark Frame Upload Buffer "} +
            std::to_wstring(index);
        result = context.upload_buffer->SetName(name.c_str());
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Object::SetName(frame upload)",
                    result));
        }

        const D3D12_RANGE no_cpu_reads{0, 0};
        void* mapped_upload = nullptr;
        result = context.upload_buffer->Map(
            0,
            &no_cpu_reads,
            &mapped_upload);
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Resource::Map(frame upload)",
                    result));
        }
        context.mapped_upload = static_cast<std::byte*>(mapped_upload);

        result = native.device->CreateCommittedResource(
            &default_heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &probe_description,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&context.probe_destination));
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Device::CreateCommittedResource(frame probe)",
                    result));
        }
        name = std::wstring{L"Shark Frame Upload Probe Destination "} +
            std::to_wstring(index);
        result = context.probe_destination->SetName(name.c_str());
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Object::SetName(frame probe)",
                    result));
        }

        D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_description{};
        descriptor_heap_description.Type =
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptor_heap_description.NumDescriptors =
            transient_descriptors_per_frame;
        descriptor_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        descriptor_heap_description.NodeMask = 0;
        result = native.device->CreateDescriptorHeap(
            &descriptor_heap_description,
            IID_PPV_ARGS(&context.descriptor_heap));
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Device::CreateDescriptorHeap(frame staging)",
                    result));
        }
        name = std::wstring{L"Shark Frame CPU Descriptor Heap "} +
            std::to_wstring(index);
        result = context.descriptor_heap->SetName(name.c_str());
        if (FAILED(result)) {
            return core::Result<Presentation>::failure(
                detail::DeviceAccess::graphics_failure(
                    device,
                    "ID3D12Object::SetName(frame descriptor heap)",
                    result));
        }
    }

    auto static_resources_result =
        implementation->create_static_cube_resources();
    if (!static_resources_result) {
        return core::Result<Presentation>::failure(
            std::move(static_resources_result).error());
    }

    auto buffer_result = implementation->acquire_back_buffers();
    if (!buffer_result) {
        return core::Result<Presentation>::failure(
            std::move(buffer_result).error());
    }
    auto depth_result = implementation->create_depth_buffer(config.extent);
    if (!depth_result) {
        return core::Result<Presentation>::failure(
            std::move(depth_result).error());
    }

    core::log_message(
        core::LogLevel::info,
        "gpu.presentation",
        std::string{"Created triple-buffered flip-discard swap chain at "} +
            std::to_string(config.extent.width) + "x" +
            std::to_string(config.extent.height) +
            " with three fence-gated frame contexts, reversed-Z depth, "
            "and the textured cube pipeline");
    return core::Result<Presentation>::success(
        Presentation{std::move(implementation)});
}

core::Result<PresentStatus> Presentation::present_frame(
    const PresentationFrameData& frame_data)
{
    if (implementation_ == nullptr) {
        return core::Result<PresentStatus>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "A moved-from Presentation cannot present"));
    }
    auto active_result = implementation_->require_active(
        "Presentation::present_frame");
    if (!active_result) {
        return core::Result<PresentStatus>::failure(
            std::move(active_result).error());
    }
    if (!valid_frame_data(frame_data)) {
        return core::Result<PresentStatus>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation frame view-projection matrix must be finite"));
    }

    const auto back_buffer_index =
        implementation_->swap_chain->GetCurrentBackBufferIndex();
    if (back_buffer_index >= back_buffer_count ||
        implementation_->back_buffers[back_buffer_index] == nullptr) {
        return core::Result<PresentStatus>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "The swap chain returned an invalid back-buffer index"));
    }

    auto context_result = implementation_->begin_frame(back_buffer_index);
    if (!context_result) {
        return core::Result<PresentStatus>::failure(
            std::move(context_result).error());
    }
    auto* const context = context_result.value();
    auto probe_result = implementation_->stage_frame_data(
        *context,
        back_buffer_index,
        frame_data);
    if (!probe_result) {
        return core::Result<PresentStatus>::failure(
            std::move(probe_result).error());
    }

    render_graph::GraphBuilder graph_builder;
    auto back_buffer_resource_result = graph_builder.import_resource(
        "BackBuffer",
        graph_back_buffer_id,
        render_graph::ResourceState::present,
        render_graph::ResourceState::present);
    if (!back_buffer_resource_result) {
        return core::Result<PresentStatus>::failure(
            std::move(back_buffer_resource_result).error());
    }
    const auto back_buffer_resource =
        back_buffer_resource_result.value();

    auto depth_buffer_resource_result = graph_builder.import_resource(
        "DepthBuffer",
        graph_depth_buffer_id,
        render_graph::ResourceState::depth_write,
        render_graph::ResourceState::depth_write);
    if (!depth_buffer_resource_result) {
        return core::Result<PresentStatus>::failure(
            std::move(depth_buffer_resource_result).error());
    }
    const auto depth_buffer_resource =
        depth_buffer_resource_result.value();

    auto cube_pass_result = graph_builder.add_pass(
        "TexturedCube",
        [implementation = implementation_.get(),
         context,
         back_buffer_index,
         back_buffer_resource,
         depth_buffer_resource](
            const render_graph::PassContext& pass_context) {
            return implementation->record_textured_cube_pass(
                pass_context,
                back_buffer_resource,
                depth_buffer_resource,
                *context,
                back_buffer_index);
        });
    if (!cube_pass_result) {
        return core::Result<PresentStatus>::failure(
            std::move(cube_pass_result).error());
    }
    const auto cube_pass = cube_pass_result.value();

    auto color_write_result = graph_builder.write(
        cube_pass,
        back_buffer_resource,
        render_graph::ResourceState::render_target);
    if (!color_write_result) {
        return core::Result<PresentStatus>::failure(
            std::move(color_write_result).error());
    }
    auto depth_write_result = graph_builder.write(
        cube_pass,
        depth_buffer_resource,
        render_graph::ResourceState::depth_write);
    if (!depth_write_result) {
        return core::Result<PresentStatus>::failure(
            std::move(depth_write_result).error());
    }

    auto compiled_graph_result = std::move(graph_builder).compile();
    if (!compiled_graph_result) {
        return core::Result<PresentStatus>::failure(
            std::move(compiled_graph_result).error());
    }
    auto compiled_graph = std::move(compiled_graph_result).value();
    ++implementation_->statistics.render_graph_compilations;
    implementation_->statistics.render_graph_resource_imports +=
        static_cast<std::uint64_t>(
            compiled_graph.stats().imported_resource_count);

    auto result = context->command_allocator->Reset();
    if (FAILED(result)) {
        return core::Result<PresentStatus>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "ID3D12CommandAllocator::Reset",
                result));
    }
    result = implementation_->command_list->Reset(
        context->command_allocator.Get(),
        implementation_->cube_pipeline.Get());
    if (FAILED(result)) {
        return core::Result<PresentStatus>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "ID3D12GraphicsCommandList::Reset",
                result));
    }

    // Buffer resources implicitly promote from COMMON for the copy and decay
    // after execution; the context fence prevents either side being reused
    // while this command is in flight.
    implementation_->command_list->CopyBufferRegion(
        context->probe_destination.Get(),
        0,
        context->upload_buffer.Get(),
        context->staged_probe_offset,
        frame_probe_bytes);

    const std::array graph_resources{
        detail::RenderGraphResourceBinding{
            graph_back_buffer_id,
            implementation_->back_buffers[back_buffer_index].Get(),
        },
        detail::RenderGraphResourceBinding{
            graph_depth_buffer_id,
            implementation_->depth_buffer.Get(),
        },
    };
    auto graph_bindings_result =
        detail::validate_render_graph_resource_bindings(
            graph_resources);
    if (!graph_bindings_result) {
        return core::Result<PresentStatus>::failure(
            std::move(graph_bindings_result).error());
    }
    detail::LegacyRenderGraphTransitionRecorder transition_recorder{
        implementation_->command_list.Get(),
        graph_resources,
    };
    auto graph_execution_result =
        compiled_graph.execute(transition_recorder);
    if (!graph_execution_result) {
        return core::Result<PresentStatus>::failure(
            std::move(graph_execution_result).error());
    }
    const auto graph_execution = graph_execution_result.value();
    if (graph_execution.transitions_recorded !=
            compiled_graph.stats().transition_count ||
        graph_execution.transitions_recorded !=
            transition_recorder.recorded_transition_count()) {
        return core::Result<PresentStatus>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "Render-graph execution recorded an inconsistent transition "
            "count"));
    }
    ++implementation_->statistics.render_graph_executions;
    implementation_->statistics.render_graph_pass_executions +=
        static_cast<std::uint64_t>(graph_execution.passes_executed);
    implementation_->statistics.render_graph_transition_barriers +=
        static_cast<std::uint64_t>(
            graph_execution.transitions_recorded);

    result = implementation_->command_list->Close();
    if (FAILED(result)) {
        return core::Result<PresentStatus>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "ID3D12GraphicsCommandList::Close",
                result));
    }
    ID3D12CommandList* command_lists[]{
        implementation_->command_list.Get(),
    };
    implementation_->command_queue->ExecuteCommandLists(1, command_lists);

    auto submit_result = implementation_->submit_frame(*context);
    if (!submit_result) {
        return core::Result<PresentStatus>::failure(
            std::move(submit_result).error());
    }
    ++implementation_->statistics.cube_draw_calls;
    implementation_->statistics.cube_indices +=
        detail::cube_indices.size();

    result = implementation_->swap_chain->Present(
        implementation_->synchronize_to_vertical_refresh ? 1 : 0,
        0);
    if (FAILED(result)) {
        return core::Result<PresentStatus>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "IDXGISwapChain::Present",
                result));
    }

    if (result == DXGI_STATUS_OCCLUDED) {
        ++implementation_->statistics.occluded_frames;
        return core::Result<PresentStatus>::success(
            PresentStatus::occluded);
    }
    if (result != S_OK) {
        return core::Result<PresentStatus>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "IDXGISwapChain::Present returned an unexpected status",
                result));
    }

    ++implementation_->statistics.presented_frames;
    return core::Result<PresentStatus>::success(PresentStatus::presented);
}

core::Result<void> Presentation::resize(
    const PresentationExtent extent)
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "A moved-from Presentation cannot resize"));
    }
    if (!valid_extent(extent)) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation extent must be nonzero and within D3D12 limits"));
    }
    auto active_result = implementation_->require_active(
        "Presentation::resize");
    if (!active_result) {
        return active_result;
    }
    if (extent == implementation_->current_extent) {
        return core::Result<void>::success();
    }

    auto idle_result = implementation_->drain_queue();
    if (!idle_result) {
        return idle_result;
    }
    implementation_->release_back_buffers();

    const auto resize_result = implementation_->swap_chain->ResizeBuffers(
        back_buffer_count,
        extent.width,
        extent.height,
        back_buffer_format,
        0);
    if (FAILED(resize_result)) {
        return core::Result<void>::failure(
            detail::DeviceAccess::graphics_failure(
                *implementation_->owner_device,
                "IDXGISwapChain::ResizeBuffers",
                resize_result));
    }

    auto extent_result = implementation_->verify_swap_chain_extent(extent);
    if (!extent_result) {
        return extent_result;
    }

    auto buffer_result = implementation_->acquire_back_buffers();
    if (!buffer_result) {
        return buffer_result;
    }
    auto depth_result = implementation_->create_depth_buffer(extent);
    if (!depth_result) {
        return depth_result;
    }
    implementation_->current_extent = extent;
    ++implementation_->statistics.resize_count;
    return core::Result<void>::success();
}

core::Result<void> Presentation::shutdown()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::success();
    }
    return implementation_->shutdown();
}

PresentationExtent Presentation::extent() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from Presentation has no extent");
    return implementation_->current_extent;
}

const PresentationStats& Presentation::stats() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from Presentation has no statistics");
    return implementation_->statistics;
}

bool Presentation::is_shutdown() const noexcept
{
    return implementation_ == nullptr ||
        implementation_->shutdown_complete;
}

} // namespace shark::rhi::d3d12
