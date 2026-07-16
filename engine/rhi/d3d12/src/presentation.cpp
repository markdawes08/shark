#include "device_access.hpp"
#include "frame_resource_state.hpp"

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <shark/core/assertion.hpp>
#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/rhi/d3d12/device.hpp>
#include <shark/rhi/d3d12/presentation.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

static_assert(back_buffer_count <= 32);

struct FrameProbe final {
    std::uint64_t frame_ordinal{};
    std::uint64_t context_generation{};
    std::uint32_t back_buffer_index{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t reserved{};
};

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
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] bool valid_clear_color(
    const ClearColor color) noexcept
{
    return std::isfinite(color.red) &&
        std::isfinite(color.green) &&
        std::isfinite(color.blue) &&
        std::isfinite(color.alpha);
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

    [[nodiscard]] core::Result<void> stage_frame_probe(
        FrameContext& context,
        const UINT back_buffer_index)
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
        const FrameProbe probe{
            statistics.frame_context_acquisitions,
            context.state.generation(),
            back_buffer_index,
            current_extent.width,
            current_extent.height,
            0,
        };
        std::memcpy(destination, &probe, sizeof(probe));

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
        for (auto& buffer : back_buffers) {
            buffer.Reset();
        }
    }

    void release_resources() noexcept
    {
        release_back_buffers();
        swap_chain.Reset();
        command_list.Reset();
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
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<IDXGISwapChain4> swap_chain;
    std::array<ComPtr<ID3D12Resource>, back_buffer_count> back_buffers;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event{};
    UINT rtv_descriptor_increment{};
    UINT transient_descriptor_increment{};
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
            "Presentation extent must be nonzero"));
    }
    if (!valid_clear_color(config.clear_color)) {
        return core::Result<Presentation>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "Presentation clear color components must be finite"));
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

    result = native.device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        implementation->frame_contexts[0].command_allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&implementation->command_list));
    if (FAILED(result)) {
        return core::Result<Presentation>::failure(
            detail::DeviceAccess::graphics_failure(
                device,
                "ID3D12Device::CreateCommandList",
                result));
    }
    result = implementation->command_list->SetName(
        L"Shark Clear Color Command List");
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

    auto buffer_result = implementation->acquire_back_buffers();
    if (!buffer_result) {
        return core::Result<Presentation>::failure(
            std::move(buffer_result).error());
    }

    core::log_message(
        core::LogLevel::info,
        "gpu.presentation",
        std::string{"Created triple-buffered flip-discard swap chain at "} +
            std::to_string(config.extent.width) + "x" +
            std::to_string(config.extent.height) +
            " with three fence-gated frame contexts");
    return core::Result<Presentation>::success(
        Presentation{std::move(implementation)});
}

core::Result<PresentStatus> Presentation::present_clear_frame()
{
    if (implementation_ == nullptr) {
        return core::Result<PresentStatus>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "A moved-from Presentation cannot present"));
    }
    auto active_result = implementation_->require_active(
        "Presentation::present_clear_frame");
    if (!active_result) {
        return core::Result<PresentStatus>::failure(
            std::move(active_result).error());
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
    auto probe_result = implementation_->stage_frame_probe(
        *context,
        back_buffer_index);
    if (!probe_result) {
        return core::Result<PresentStatus>::failure(
            std::move(probe_result).error());
    }

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
        nullptr);
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

    D3D12_RESOURCE_BARRIER to_render_target{};
    to_render_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_render_target.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    to_render_target.Transition.pResource =
        implementation_->back_buffers[back_buffer_index].Get();
    to_render_target.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_render_target.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_render_target.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;
    implementation_->command_list->ResourceBarrier(1, &to_render_target);

    auto render_target =
        implementation_->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    render_target.ptr += static_cast<SIZE_T>(back_buffer_index) *
        static_cast<SIZE_T>(implementation_->rtv_descriptor_increment);
    implementation_->command_list->OMSetRenderTargets(
        1,
        &render_target,
        FALSE,
        nullptr);
    const std::array<float, 4> clear_color{
        implementation_->clear_color.red,
        implementation_->clear_color.green,
        implementation_->clear_color.blue,
        implementation_->clear_color.alpha,
    };
    implementation_->command_list->ClearRenderTargetView(
        render_target,
        clear_color.data(),
        0,
        nullptr);

    auto to_present = to_render_target;
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    implementation_->command_list->ResourceBarrier(1, &to_present);

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
            "Presentation extent must be nonzero"));
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
