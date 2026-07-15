#include "options.hpp"

#include <shark/core/error.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace shark::sandbox {
namespace {

[[nodiscard]] core::Result<Options> invalid_options(std::string message)
{
    return core::Result<Options>::failure(core::Error{
        core::ErrorCategory::core,
        core::ErrorCode::invalid_argument,
        std::move(message),
    });
}

} // namespace

core::Result<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    bool run_mode_seen = false;
    bool adapter_seen = false;
    bool validation_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--platform-smoke" ||
            argument == "--gpu-smoke" ||
            argument == "--present-smoke" ||
            argument == "--capabilities") {
            if (run_mode_seen) {
                return invalid_options(
                    "Only one run mode may be specified");
            }
            run_mode_seen = true;
            if (argument == "--platform-smoke") {
                options.run_mode = RunMode::platform_smoke;
            }
            else if (argument == "--gpu-smoke") {
                options.run_mode = RunMode::gpu_smoke;
            }
            else if (argument == "--present-smoke") {
                options.run_mode = RunMode::present_smoke;
            }
            else {
                options.run_mode = RunMode::capabilities;
            }
            continue;
        }

        if (argument == "--warp") {
            if (adapter_seen) {
                return invalid_options(
                    "Only one GPU adapter selector may be specified");
            }
            adapter_seen = true;
            options.adapter = rhi::d3d12::AdapterSelection::warp();
            continue;
        }

        if (argument == "--adapter") {
            if (adapter_seen) {
                return invalid_options(
                    "Only one GPU adapter selector may be specified");
            }
            if (index + 1 >= arguments.size()) {
                return invalid_options(
                    "--adapter requires a zero-based candidate index");
            }

            const auto value = arguments[++index];
            std::uint64_t parsed_index = 0;
            const auto* const begin = value.data();
            const auto* const end = begin + value.size();
            const auto parse_result = std::from_chars(
                begin,
                end,
                parsed_index,
                10);
            if (value.empty() ||
                parse_result.ec != std::errc{} ||
                parse_result.ptr != end ||
                parsed_index >
                    std::numeric_limits<std::uint32_t>::max()) {
                return invalid_options(
                    "--adapter requires an unsigned decimal 32-bit index");
            }

            adapter_seen = true;
            options.adapter =
                rhi::d3d12::AdapterSelection::by_preference_index(
                    static_cast<std::uint32_t>(parsed_index));
            continue;
        }

        if (argument == "--gpu-validation") {
            if (validation_seen) {
                return invalid_options(
                    "--gpu-validation may be specified only once");
            }
            validation_seen = true;
            options.gpu_based_validation = true;
            continue;
        }

        return invalid_options(
            std::string{"Unknown argument: "} + std::string{argument});
    }

    if (options.run_mode == RunMode::platform_smoke &&
        (adapter_seen || validation_seen)) {
        return invalid_options(
            "GPU selectors and validation cannot be used with "
            "--platform-smoke");
    }

    return core::Result<Options>::success(options);
}

} // namespace shark::sandbox
