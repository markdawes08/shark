#pragma once

#include <shark/core/error.hpp>

#include <directx/d3d12.h>
#include <dxgi1_6.h>

#include <string_view>

namespace shark::rhi::d3d12 {

class Device;

namespace detail {

struct NativeDeviceContext final {
    ID3D12Device* device{};
    IDXGIFactory7* factory{};
};

class DeviceAccess final {
public:
    [[nodiscard]] static NativeDeviceContext native_context(
        Device& device) noexcept;

    [[nodiscard]] static core::Error graphics_failure(
        Device& device,
        std::string_view operation,
        HRESULT result);
};

} // namespace detail
} // namespace shark::rhi::d3d12
