#pragma once

#include <shark/core/result.hpp>
#include <shark/rhi/d3d12/device.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace shark::sandbox {

enum class RunMode : std::uint8_t {
    interactive = 1,
    platform_smoke,
    gpu_smoke,
    present_smoke,
    capabilities,
};

struct Options final {
    RunMode run_mode{RunMode::interactive};
    rhi::d3d12::AdapterSelection adapter{
        rhi::d3d12::AdapterSelection::high_performance()};
    bool gpu_based_validation{};
};

[[nodiscard]] core::Result<Options> parse_options(
    std::span<const std::string_view> arguments);

[[nodiscard]] constexpr std::string_view usage() noexcept
{
    return "Usage: SharkSandbox "
        "[--platform-smoke | --gpu-smoke | --present-smoke | "
        "--capabilities] "
        "[--warp | --adapter <index>] [--gpu-validation]";
}

} // namespace shark::sandbox
