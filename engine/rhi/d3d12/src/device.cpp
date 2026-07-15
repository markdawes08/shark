#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#include "device_access.hpp"

#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>
#include <shark/rhi/d3d12/device.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace shark::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr D3D_SHADER_MODEL maximum_retail_shader_model =
    D3D_SHADER_MODEL_6_9;
constexpr ShaderModel required_shader_model{6, 0};
constexpr std::uint64_t bytes_per_mebibyte = 1024ULL * 1024ULL;
constexpr UINT64 debug_message_limit = 1024;
constexpr std::size_t maximum_dred_nodes = 64;
constexpr std::size_t maximum_dred_contexts = 16;
constexpr UINT maximum_dred_history_operations = 65'536;
std::atomic_flag device_creation_started = ATOMIC_FLAG_INIT;

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

[[nodiscard]] std::string hexadecimal(
    const std::uint64_t value,
    const int width)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(width) << value;
    return stream.str();
}

[[nodiscard]] std::string hresult_message(
    const std::string_view operation,
    const HRESULT result)
{
    auto message = std::string{operation};
    message.append(" failed with HRESULT ");
    message.append(hexadecimal(
        static_cast<std::uint32_t>(result),
        8));
    return message;
}

[[nodiscard]] std::string windows_error_message(
    const std::string_view operation,
    const DWORD error)
{
    auto message = std::string{operation};
    message.append(" failed with Windows error ");
    message.append(std::to_string(error));
    return message;
}

[[nodiscard]] std::string utf8_from_wide(const std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }

    const auto source_size = static_cast<int>(value.size());
    const auto required_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0) {
        return "<invalid UTF-16>";
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    const auto converted_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        result.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size) {
        return "<invalid UTF-16>";
    }
    return result;
}

[[nodiscard]] core::Result<std::filesystem::path> module_path(
    const HMODULE module)
{
    constexpr DWORD maximum_path_length = 32'768;
    std::wstring buffer(maximum_path_length, L'\0');
    SetLastError(ERROR_SUCCESS);
    const auto length = GetModuleFileNameW(
        module,
        buffer.data(),
        maximum_path_length);
    if (length == 0 || length >= maximum_path_length) {
        return core::Result<std::filesystem::path>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            windows_error_message("GetModuleFileNameW", GetLastError())));
    }
    buffer.resize(length);
    return core::Result<std::filesystem::path>::success(
        std::filesystem::path{std::move(buffer)});
}

[[nodiscard]] core::Result<std::filesystem::path> executable_directory()
{
    auto executable_path_result = module_path(nullptr);
    if (!executable_path_result) {
        return core::Result<std::filesystem::path>::failure(
            std::move(executable_path_result).error());
    }
    auto executable_path = std::move(executable_path_result).value();
    return core::Result<std::filesystem::path>::success(
        executable_path.parent_path());
}

struct ModuleCloser final {
    void operator()(const HMODULE module) const noexcept
    {
        if (module != nullptr) {
            static_cast<void>(FreeLibrary(module));
        }
    }
};

using UniqueModule = std::unique_ptr<
    std::remove_pointer_t<HMODULE>,
    ModuleCloser>;

struct LoadedWarp final {
    UniqueModule module;
    std::string path;
};

[[nodiscard]] core::Result<void> verify_agility_exports()
{
    const auto executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            windows_error_message("GetModuleHandleW(executable)",
                GetLastError())));
    }

    const auto version_address = GetProcAddress(
        executable,
        "D3D12SDKVersion");
    const auto path_address = GetProcAddress(
        executable,
        "D3D12SDKPath");
    if (version_address == nullptr || path_address == nullptr) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "The main executable must export D3D12SDKVersion and "
            "D3D12SDKPath"));
    }

    const auto* const exported_version =
        reinterpret_cast<const UINT*>(version_address);
    const auto* const exported_path =
        reinterpret_cast<const char* const*>(path_address);
    if (*exported_version != D3D12_SDK_VERSION ||
        *exported_path == nullptr ||
        std::string_view{*exported_path} != ".\\D3D12\\") {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "The executable's Agility SDK exports do not match the "
            "pinned SDK 619 runtime layout"));
    }
    return core::Result<void>::success();
}

[[nodiscard]] core::Result<LoadedWarp> load_packaged_warp()
{
    auto directory_result = executable_directory();
    if (!directory_result) {
        return core::Result<LoadedWarp>::failure(
            std::move(directory_result).error());
    }
    const auto expected_path =
        std::move(directory_result).value() / L"d3d10warp.dll";

    SetLastError(ERROR_SUCCESS);
    const auto module = LoadLibraryExW(
        expected_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return core::Result<LoadedWarp>::failure(graphics_error(
            core::ErrorCode::unavailable,
            windows_error_message(
                "LoadLibraryExW(app-local d3d10warp.dll)",
                GetLastError())));
    }

    LoadedWarp loaded{UniqueModule{module}, {}};
    auto actual_path_result = module_path(module);
    if (!actual_path_result) {
        return core::Result<LoadedWarp>::failure(
            std::move(actual_path_result).error());
    }
    const auto actual_path = std::move(actual_path_result).value();

    std::error_code comparison_error;
    const auto is_expected_module = std::filesystem::equivalent(
        expected_path,
        actual_path,
        comparison_error);
    if (comparison_error || !is_expected_module) {
        auto message = std::string{
            "WARP did not load from the pinned app-local path; loaded "};
        message.append(utf8_from_wide(actual_path.wstring()));
        message.append(", expected ");
        message.append(utf8_from_wide(expected_path.wstring()));
        return core::Result<LoadedWarp>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            std::move(message)));
    }

    loaded.path = utf8_from_wide(actual_path.wstring());
    return core::Result<LoadedWarp>::success(std::move(loaded));
}

[[nodiscard]] core::Result<std::string> loaded_agility_runtime_path()
{
    const auto module = GetModuleHandleW(L"D3D12Core.dll");
    if (module == nullptr) {
        return core::Result<std::string>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "D3D12Core.dll was not loaded after Direct3D 12 setup"));
    }

    auto path_result = module_path(module);
    if (!path_result) {
        return core::Result<std::string>::failure(
            std::move(path_result).error());
    }
    const auto path = std::move(path_result).value();
    return core::Result<std::string>::success(
        utf8_from_wide(path.wstring()));
}

[[nodiscard]] core::Result<void> configure_debug_layer(
    const bool enabled,
    const bool gpu_based_validation)
{
    if (gpu_based_validation && !enabled) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "GPU-based validation requires the D3D12 debug layer"));
    }
    if (!enabled) {
        return core::Result<void>::success();
    }

    ComPtr<ID3D12Debug> debug;
    const auto debug_result = D3D12GetDebugInterface(
        IID_PPV_ARGS(&debug));
    if (FAILED(debug_result)) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::unavailable,
            hresult_message("D3D12GetDebugInterface(ID3D12Debug)",
                debug_result)));
    }
    debug->EnableDebugLayer();

    if (gpu_based_validation) {
        ComPtr<ID3D12Debug1> debug_one;
        const auto interface_result = debug.As(&debug_one);
        if (FAILED(interface_result)) {
            return core::Result<void>::failure(graphics_error(
                core::ErrorCode::unsupported,
                hresult_message("QueryInterface(ID3D12Debug1)",
                    interface_result)));
        }
        debug_one->SetEnableGPUBasedValidation(TRUE);
        debug_one->SetEnableSynchronizedCommandQueueValidation(TRUE);
    }

    return core::Result<void>::success();
}

[[nodiscard]] core::Result<void> configure_dred()
{
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
    const auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&settings));
    if (FAILED(result)) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::unavailable,
            hresult_message(
                "D3D12GetDebugInterface(DRED settings)",
                result)));
    }

    settings->SetAutoBreadcrumbsEnablement(
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetBreadcrumbContextEnablement(
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    return core::Result<void>::success();
}

[[nodiscard]] ShaderModel shader_model_from_native(
    const D3D_SHADER_MODEL shader_model) noexcept
{
    const auto value = static_cast<std::uint32_t>(shader_model);
    return ShaderModel{
        static_cast<std::uint16_t>(value >> 4U),
        static_cast<std::uint16_t>(value & 0x0FU),
    };
}

[[nodiscard]] bool shader_model_at_least(
    const ShaderModel actual,
    const ShaderModel required) noexcept
{
    return actual.major > required.major ||
        (actual.major == required.major &&
         actual.minor >= required.minor);
}

[[nodiscard]] core::Result<ShaderModel> query_shader_model(
    ID3D12Device* const device)
{
    for (auto value = static_cast<std::uint32_t>(
             maximum_retail_shader_model);
         value >= static_cast<std::uint32_t>(D3D_SHADER_MODEL_6_0);
         --value) {
        D3D12_FEATURE_DATA_SHADER_MODEL feature{
            static_cast<D3D_SHADER_MODEL>(value),
        };
        const auto result = device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL,
            &feature,
            static_cast<UINT>(sizeof(feature)));
        if (SUCCEEDED(result)) {
            return core::Result<ShaderModel>::success(
                shader_model_from_native(feature.HighestShaderModel));
        }
        if (result != E_INVALIDARG) {
            return core::Result<ShaderModel>::failure(graphics_error(
                core::ErrorCode::operation_failed,
                hresult_message("CheckFeatureSupport(shader model)",
                    result)));
        }
    }

    return core::Result<ShaderModel>::failure(graphics_error(
        core::ErrorCode::unsupported,
        "The adapter did not report Shader Model 6.0 support"));
}

struct Candidate final {
    ComPtr<IDXGIAdapter4> adapter;
    AdapterInfo info;
};

[[nodiscard]] core::Result<Candidate> describe_adapter(
    ComPtr<IDXGIAdapter4> adapter,
    const std::optional<std::uint32_t> preference_index)
{
    DXGI_ADAPTER_DESC3 description{};
    const auto result = adapter->GetDesc3(&description);
    if (FAILED(result)) {
        return core::Result<Candidate>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("IDXGIAdapter4::GetDesc3", result)));
    }

    AdapterInfo info;
    info.preference_index = preference_index;
    info.name = utf8_from_wide(description.Description);
    info.luid = AdapterLuid{
        description.AdapterLuid.LowPart,
        description.AdapterLuid.HighPart,
    };
    info.vendor_id = description.VendorId;
    info.device_id = description.DeviceId;
    info.subsystem_id = description.SubSysId;
    info.revision = description.Revision;
    info.dedicated_video_memory = static_cast<std::uint64_t>(
        description.DedicatedVideoMemory);
    info.dedicated_system_memory = static_cast<std::uint64_t>(
        description.DedicatedSystemMemory);
    info.shared_system_memory = static_cast<std::uint64_t>(
        description.SharedSystemMemory);
    info.software =
        (description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0;
    info.remote =
        (description.Flags & DXGI_ADAPTER_FLAG3_REMOTE) != 0;
    return core::Result<Candidate>::success(Candidate{
        std::move(adapter),
        std::move(info),
    });
}

void probe_required_capabilities(Candidate& candidate)
{
    ComPtr<ID3D12Device> probe_device;
    const auto device_result = D3D12CreateDevice(
        candidate.adapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&probe_device));
    if (FAILED(device_result)) {
        candidate.info.rejection_reason =
            hresult_message("Feature Level 12_0 device probe",
                device_result);
        return;
    }
    candidate.info.supports_feature_level_12_0 = true;

    auto shader_model_result = query_shader_model(probe_device.Get());
    if (!shader_model_result) {
        candidate.info.rejection_reason =
            std::move(shader_model_result).error().message();
        return;
    }
    candidate.info.highest_shader_model =
        std::move(shader_model_result).value();
    if (!supports_required_baseline(candidate.info)) {
        candidate.info.rejection_reason =
            "Shader Model 6.0 is required";
    }
}

[[nodiscard]] core::Result<std::vector<Candidate>> enumerate_adapters(
    IDXGIFactory7* const factory)
{
    std::vector<Candidate> candidates;
    for (std::uint32_t index = 0;; ++index) {
        ComPtr<IDXGIAdapter4> adapter;
        const auto result = factory->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            return core::Result<std::vector<Candidate>>::failure(
                graphics_error(
                    core::ErrorCode::operation_failed,
                    hresult_message(
                        "IDXGIFactory7::EnumAdapterByGpuPreference",
                        result)));
        }

        auto candidate_result = describe_adapter(
            std::move(adapter),
            index);
        if (!candidate_result) {
            return core::Result<std::vector<Candidate>>::failure(
                std::move(candidate_result).error());
        }
        auto candidate = std::move(candidate_result).value();
        probe_required_capabilities(candidate);
        candidates.push_back(std::move(candidate));
    }
    return core::Result<std::vector<Candidate>>::success(
        std::move(candidates));
}

[[nodiscard]] std::string shader_model_text(
    const std::optional<ShaderModel>& shader_model)
{
    if (!shader_model.has_value()) {
        return "unavailable";
    }
    return std::to_string(shader_model->major) + "." +
        std::to_string(shader_model->minor);
}

[[nodiscard]] std::string adapter_flags_text(
    const AdapterInfo& adapter)
{
    if (!adapter.software && !adapter.remote) {
        return "hardware";
    }
    if (adapter.software && adapter.remote) {
        return "software,remote";
    }
    return adapter.software ? "software" : "remote";
}

void log_adapter(const AdapterInfo& adapter)
{
    auto message = adapter.preference_index.has_value()
        ? std::string{"["} +
            std::to_string(*adapter.preference_index) + "] "
        : std::string{"[WARP] "};
    message.append(adapter.name);
    message.append("; LUID=");
    message.append(hexadecimal(
        static_cast<std::uint32_t>(adapter.luid.high_part),
        8));
    message.push_back(':');
    message.append(hexadecimal(adapter.luid.low_part, 8));
    message.append("; vendor=");
    message.append(hexadecimal(adapter.vendor_id, 4));
    message.append("; device=");
    message.append(hexadecimal(adapter.device_id, 4));
    message.append("; dedicated-vram=");
    message.append(std::to_string(
        adapter.dedicated_video_memory / bytes_per_mebibyte));
    message.append(" MiB; flags=");
    message.append(adapter_flags_text(adapter));
    message.append("; FL12_0=");
    message.append(adapter.supports_feature_level_12_0 ? "yes" : "no");
    message.append("; SM=");
    message.append(shader_model_text(adapter.highest_shader_model));
    message.append("; baseline=");
    message.append(supports_required_baseline(adapter) ? "yes" : "no");
    if (!adapter.rejection_reason.empty()) {
        message.append("; reason=");
        message.append(adapter.rejection_reason);
    }
    core::log_message(
        core::LogLevel::info,
        "gpu.adapter",
        message);
}

[[nodiscard]] core::Result<Candidate> select_hardware_candidate(
    const AdapterSelection selection,
    const std::vector<Candidate>& candidates)
{
    if (selection.kind == AdapterSelectionKind::preference_index) {
        if (selection.preference_index >= candidates.size()) {
            return core::Result<Candidate>::failure(graphics_error(
                core::ErrorCode::not_found,
                "The requested adapter index was not enumerated"));
        }
        const auto& candidate = candidates[selection.preference_index];
        if (candidate.info.software || candidate.info.remote) {
            return core::Result<Candidate>::failure(graphics_error(
                core::ErrorCode::unsupported,
                "The requested adapter is not a local hardware adapter"));
        }
        if (!supports_required_baseline(candidate.info)) {
            auto message = std::string{
                "The requested adapter does not meet the Shark baseline"};
            if (!candidate.info.rejection_reason.empty()) {
                message.append(": ");
                message.append(candidate.info.rejection_reason);
            }
            return core::Result<Candidate>::failure(graphics_error(
                core::ErrorCode::unsupported,
                std::move(message)));
        }
        return core::Result<Candidate>::success(candidate);
    }

    for (const auto& candidate : candidates) {
        if (!candidate.info.software &&
            !candidate.info.remote &&
            supports_required_baseline(candidate.info)) {
            return core::Result<Candidate>::success(candidate);
        }
    }
    return core::Result<Candidate>::failure(graphics_error(
        core::ErrorCode::unavailable,
        "No local hardware adapter meets Feature Level 12_0 and "
        "Shader Model 6.0"));
}

[[nodiscard]] core::Result<Candidate> select_warp_candidate(
    IDXGIFactory7* const factory)
{
    ComPtr<IDXGIAdapter4> adapter;
    const auto result = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    if (FAILED(result)) {
        return core::Result<Candidate>::failure(graphics_error(
            core::ErrorCode::unavailable,
            hresult_message("IDXGIFactory4::EnumWarpAdapter", result)));
    }

    auto candidate_result = describe_adapter(std::move(adapter), std::nullopt);
    if (!candidate_result) {
        return core::Result<Candidate>::failure(
            std::move(candidate_result).error());
    }
    auto candidate = std::move(candidate_result).value();
    probe_required_capabilities(candidate);
    if (!supports_required_baseline(candidate.info)) {
        auto message = std::string{
            "The packaged WARP adapter does not meet the Shark baseline"};
        if (!candidate.info.rejection_reason.empty()) {
            message.append(": ");
            message.append(candidate.info.rejection_reason);
        }
        return core::Result<Candidate>::failure(graphics_error(
            core::ErrorCode::unsupported,
            std::move(message)));
    }
    return core::Result<Candidate>::success(std::move(candidate));
}

[[nodiscard]] std::pair<std::uint16_t, std::uint16_t>
feature_level_version(const D3D_FEATURE_LEVEL feature_level) noexcept
{
    switch (feature_level) {
    case D3D_FEATURE_LEVEL_12_2:
        return {12, 2};
    case D3D_FEATURE_LEVEL_12_1:
        return {12, 1};
    case D3D_FEATURE_LEVEL_12_0:
        return {12, 0};
    default:
        return {0, 0};
    }
}

[[nodiscard]] TierVersion tier_version_from_tenths(
    const std::uint32_t value) noexcept
{
    return TierVersion{
        static_cast<std::uint16_t>(value / 10U),
        static_cast<std::uint16_t>(value % 10U),
    };
}

[[nodiscard]] TierVersion sampler_feedback_version(
    const D3D12_SAMPLER_FEEDBACK_TIER tier) noexcept
{
    switch (tier) {
    case D3D12_SAMPLER_FEEDBACK_TIER_0_9:
        return TierVersion{0, 9};
    case D3D12_SAMPLER_FEEDBACK_TIER_1_0:
        return TierVersion{1, 0};
    case D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED:
    default:
        return TierVersion{};
    }
}

template<typename FeatureData>
[[nodiscard]] bool query_optional_feature(
    ID3D12Device* const device,
    const D3D12_FEATURE feature,
    FeatureData& data,
    const std::string_view name)
{
    const auto result = device->CheckFeatureSupport(
        feature,
        &data,
        static_cast<UINT>(sizeof(data)));
    if (SUCCEEDED(result)) {
        return true;
    }

    core::log_message(
        core::LogLevel::warning,
        "gpu.caps",
        hresult_message(
            std::string{"Optional capability query "} +
                std::string{name},
            result));
    return false;
}

[[nodiscard]] core::Result<RendererCaps> query_capabilities(
    ID3D12Device* const device)
{
    constexpr std::array feature_levels{
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_level_data{
        static_cast<UINT>(feature_levels.size()),
        feature_levels.data(),
        D3D_FEATURE_LEVEL_12_0,
    };
    const auto feature_level_result = device->CheckFeatureSupport(
        D3D12_FEATURE_FEATURE_LEVELS,
        &feature_level_data,
        static_cast<UINT>(sizeof(feature_level_data)));
    if (FAILED(feature_level_result)) {
        return core::Result<RendererCaps>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("CheckFeatureSupport(feature levels)",
                feature_level_result)));
    }

    auto shader_model_result = query_shader_model(device);
    if (!shader_model_result) {
        return core::Result<RendererCaps>::failure(
            std::move(shader_model_result).error());
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS base_options{};
    const auto base_result = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS,
        &base_options,
        static_cast<UINT>(sizeof(base_options)));
    if (FAILED(base_result)) {
        return core::Result<RendererCaps>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("CheckFeatureSupport(base options)",
                base_result)));
    }

    RendererCaps capabilities;
    const auto [feature_major, feature_minor] = feature_level_version(
        feature_level_data.MaxSupportedFeatureLevel);
    capabilities.maximum_feature_level_major = feature_major;
    capabilities.maximum_feature_level_minor = feature_minor;
    capabilities.highest_shader_model =
        std::move(shader_model_result).value();
    capabilities.resource_binding_tier = static_cast<std::uint32_t>(
        base_options.ResourceBindingTier);
    capabilities.resource_heap_tier = static_cast<std::uint32_t>(
        base_options.ResourceHeapTier);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options_five{};
    if (query_optional_feature(
            device,
            D3D12_FEATURE_D3D12_OPTIONS5,
            options_five,
            "D3D12_OPTIONS5")) {
        capabilities.raytracing_tier = tier_version_from_tenths(
            static_cast<std::uint32_t>(options_five.RaytracingTier));
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options_six{};
    if (query_optional_feature(
            device,
            D3D12_FEATURE_D3D12_OPTIONS6,
            options_six,
            "D3D12_OPTIONS6")) {
        capabilities.variable_shading_rate_tier =
            static_cast<std::uint32_t>(
                options_six.VariableShadingRateTier);
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options_seven{};
    if (query_optional_feature(
            device,
            D3D12_FEATURE_D3D12_OPTIONS7,
            options_seven,
            "D3D12_OPTIONS7")) {
        capabilities.mesh_shader_tier = tier_version_from_tenths(
            static_cast<std::uint32_t>(options_seven.MeshShaderTier));
        capabilities.sampler_feedback_tier = sampler_feedback_version(
            options_seven.SamplerFeedbackTier);
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options_twelve{};
    if (query_optional_feature(
            device,
            D3D12_FEATURE_D3D12_OPTIONS12,
            options_twelve,
            "D3D12_OPTIONS12")) {
        capabilities.enhanced_barriers =
            options_twelve.EnhancedBarriersSupported != FALSE;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options_twenty_one{};
    if (query_optional_feature(
            device,
            D3D12_FEATURE_D3D12_OPTIONS21,
            options_twenty_one,
            "D3D12_OPTIONS21")) {
        capabilities.work_graphs_tier = tier_version_from_tenths(
            static_cast<std::uint32_t>(options_twenty_one.WorkGraphsTier));
    }

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    architecture.NodeIndex = 0;
    if (query_optional_feature(
            device,
            D3D12_FEATURE_ARCHITECTURE1,
            architecture,
            "ARCHITECTURE1")) {
        capabilities.unified_memory_architecture =
            architecture.UMA != FALSE;
        capabilities.cache_coherent_unified_memory_architecture =
            architecture.CacheCoherentUMA != FALSE;
    }

    return core::Result<RendererCaps>::success(
        std::move(capabilities));
}

[[nodiscard]] std::string optional_bool_text(
    const std::optional<bool> value)
{
    if (!value.has_value()) {
        return "unavailable";
    }
    return *value ? "yes" : "no";
}

[[nodiscard]] std::string optional_tier_text(
    const std::optional<std::uint32_t> value)
{
    return value.has_value() ? std::to_string(*value) : "unavailable";
}

[[nodiscard]] std::string optional_version_text(
    const std::optional<TierVersion>& value)
{
    if (!value.has_value()) {
        return "unavailable";
    }
    if (value->major == 0 && value->minor == 0) {
        return "not supported";
    }
    return std::to_string(value->major) + "." +
        std::to_string(value->minor);
}

void log_capabilities(const RendererCaps& capabilities)
{
    auto baseline = std::string{"Feature Level "};
    baseline.append(std::to_string(
        capabilities.maximum_feature_level_major));
    baseline.push_back('.');
    baseline.append(std::to_string(
        capabilities.maximum_feature_level_minor));
    baseline.append("; Shader Model ");
    baseline.append(std::to_string(
        capabilities.highest_shader_model.major));
    baseline.push_back('.');
    baseline.append(std::to_string(
        capabilities.highest_shader_model.minor));
    baseline.append("; resource binding tier ");
    baseline.append(std::to_string(
        capabilities.resource_binding_tier));
    baseline.append("; resource heap tier ");
    baseline.append(std::to_string(capabilities.resource_heap_tier));
    core::log_message(core::LogLevel::info, "gpu.caps", baseline);

    auto optional = std::string{"Enhanced barriers="};
    optional.append(optional_bool_text(capabilities.enhanced_barriers));
    optional.append("; DXR tier=");
    optional.append(optional_version_text(capabilities.raytracing_tier));
    optional.append("; mesh shader tier=");
    optional.append(optional_version_text(capabilities.mesh_shader_tier));
    optional.append("; VRS tier=");
    optional.append(optional_tier_text(
        capabilities.variable_shading_rate_tier));
    optional.append("; sampler feedback tier=");
    optional.append(optional_version_text(
        capabilities.sampler_feedback_tier));
    optional.append("; work graphs tier=");
    optional.append(optional_version_text(capabilities.work_graphs_tier));
    core::log_message(core::LogLevel::info, "gpu.caps", optional);

    auto memory = std::string{"UMA="};
    memory.append(optional_bool_text(
        capabilities.unified_memory_architecture));
    memory.append("; cache-coherent UMA=");
    memory.append(optional_bool_text(
        capabilities.cache_coherent_unified_memory_architecture));
    core::log_message(core::LogLevel::info, "gpu.caps", memory);
}

[[nodiscard]] bool is_live_child_message(
    const D3D12_MESSAGE_ID identifier) noexcept
{
    switch (identifier) {
    case D3D12_MESSAGE_ID_LIVE_SWAPCHAIN:
    case D3D12_MESSAGE_ID_LIVE_COMMANDQUEUE:
    case D3D12_MESSAGE_ID_LIVE_COMMANDALLOCATOR:
    case D3D12_MESSAGE_ID_LIVE_COMMANDLIST12:
    case D3D12_MESSAGE_ID_LIVE_RESOURCE:
    case D3D12_MESSAGE_ID_LIVE_DESCRIPTORHEAP:
    case D3D12_MESSAGE_ID_LIVE_MONITOREDFENCE:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] core::Result<DebugMessageCounts> inspect_debug_messages(
    ID3D12InfoQueue* const info_queue,
    const UINT64 first_message = 0,
    std::uint64_t* const live_child_count = nullptr)
{
    DebugMessageCounts counts;
    if (info_queue == nullptr) {
        return core::Result<DebugMessageCounts>::success(counts);
    }

    const auto message_count = info_queue->GetNumStoredMessages();
    if (first_message > message_count) {
        return core::Result<DebugMessageCounts>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "The D3D12 validation message cursor moved past storage"));
    }
    for (UINT64 index = first_message; index < message_count; ++index) {
        SIZE_T byte_count = 0;
        auto result = info_queue->GetMessage(index, nullptr, &byte_count);
        if (FAILED(result)) {
            return core::Result<DebugMessageCounts>::failure(graphics_error(
                core::ErrorCode::operation_failed,
                hresult_message("ID3D12InfoQueue::GetMessage(size)",
                    result)));
        }

        std::vector<std::byte> storage(byte_count);
        auto* const message = reinterpret_cast<D3D12_MESSAGE*>(
            storage.data());
        result = info_queue->GetMessage(index, message, &byte_count);
        if (FAILED(result)) {
            return core::Result<DebugMessageCounts>::failure(graphics_error(
                core::ErrorCode::operation_failed,
                hresult_message("ID3D12InfoQueue::GetMessage", result)));
        }

        auto description_length = message->DescriptionByteLength;
        if (description_length > 0 &&
            message->pDescription[description_length - 1] == '\0') {
            --description_length;
        }
        const auto description = std::string{
            message->pDescription,
            description_length,
        };
        if (live_child_count != nullptr &&
            is_live_child_message(message->ID)) {
            ++(*live_child_count);
        }

        switch (message->Severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            ++counts.corruption;
            core::log_message(
                core::LogLevel::critical,
                "gpu.validation",
                description);
            break;
        case D3D12_MESSAGE_SEVERITY_ERROR:
            ++counts.error;
            core::log_message(
                core::LogLevel::error,
                "gpu.validation",
                description);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            ++counts.warning;
            core::log_message(
                core::LogLevel::warning,
                "gpu.validation",
                description);
            break;
        case D3D12_MESSAGE_SEVERITY_INFO:
            ++counts.info;
            break;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
            ++counts.message;
            break;
        default:
            break;
        }
    }

    return core::Result<DebugMessageCounts>::success(counts);
}

[[nodiscard]] core::Result<DebugMessageCounts> inspect_dxgi_messages(
    IDXGIInfoQueue* const info_queue,
    const UINT64 first_message = 0)
{
    DebugMessageCounts counts;
    if (info_queue == nullptr) {
        return core::Result<DebugMessageCounts>::success(counts);
    }

    const auto message_count = info_queue->GetNumStoredMessages(
        DXGI_DEBUG_DXGI);
    if (first_message > message_count) {
        return core::Result<DebugMessageCounts>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "The DXGI validation message cursor moved past storage"));
    }
    for (UINT64 index = first_message; index < message_count; ++index) {
        SIZE_T byte_count = 0;
        auto result = info_queue->GetMessage(
            DXGI_DEBUG_DXGI,
            index,
            nullptr,
            &byte_count);
        if (FAILED(result)) {
            return core::Result<DebugMessageCounts>::failure(
                graphics_error(
                    core::ErrorCode::operation_failed,
                    hresult_message(
                        "IDXGIInfoQueue::GetMessage(size)",
                        result)));
        }

        std::vector<std::byte> storage(byte_count);
        auto* const message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(
            storage.data());
        result = info_queue->GetMessage(
            DXGI_DEBUG_DXGI,
            index,
            message,
            &byte_count);
        if (FAILED(result)) {
            return core::Result<DebugMessageCounts>::failure(
                graphics_error(
                    core::ErrorCode::operation_failed,
                    hresult_message("IDXGIInfoQueue::GetMessage", result)));
        }

        auto description_length = message->DescriptionByteLength;
        if (description_length > 0 &&
            message->pDescription[description_length - 1] == '\0') {
            --description_length;
        }
        auto description = std::string{"DXGI: "};
        description.append(message->pDescription, description_length);

        switch (message->Severity) {
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION:
            ++counts.corruption;
            core::log_message(
                core::LogLevel::critical,
                "gpu.validation",
                description);
            break;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:
            ++counts.error;
            core::log_message(
                core::LogLevel::error,
                "gpu.validation",
                description);
            break;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:
            ++counts.warning;
            core::log_message(
                core::LogLevel::warning,
                "gpu.validation",
                description);
            break;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:
            ++counts.info;
            break;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE:
            ++counts.message;
            break;
        default:
            break;
        }
    }

    return core::Result<DebugMessageCounts>::success(counts);
}

void accumulate_message_counts(
    DebugMessageCounts& destination,
    const DebugMessageCounts& source) noexcept
{
    destination.corruption += source.corruption;
    destination.error += source.error;
    destination.warning += source.warning;
    destination.info += source.info;
    destination.message += source.message;
}

[[nodiscard]] core::Result<ComPtr<ID3D12InfoQueue>> configure_info_queue(
    ID3D12Device* const device,
    const bool debug_layer_enabled)
{
    if (!debug_layer_enabled) {
        return core::Result<ComPtr<ID3D12InfoQueue>>::success({});
    }

    ComPtr<ID3D12InfoQueue> info_queue;
    const auto interface_result = device->QueryInterface(
        IID_PPV_ARGS(&info_queue));
    if (FAILED(interface_result)) {
        return core::Result<ComPtr<ID3D12InfoQueue>>::failure(
            graphics_error(
                core::ErrorCode::unavailable,
                hresult_message("QueryInterface(ID3D12InfoQueue)",
                    interface_result)));
    }

    const auto limit_result = info_queue->SetMessageCountLimit(
        debug_message_limit);
    if (FAILED(limit_result)) {
        return core::Result<ComPtr<ID3D12InfoQueue>>::failure(
            graphics_error(
                core::ErrorCode::operation_failed,
                hresult_message(
                    "ID3D12InfoQueue::SetMessageCountLimit",
                    limit_result)));
    }

    if (IsDebuggerPresent() != FALSE) {
        const auto corruption_result = info_queue->SetBreakOnSeverity(
            D3D12_MESSAGE_SEVERITY_CORRUPTION,
            TRUE);
        const auto error_result = info_queue->SetBreakOnSeverity(
            D3D12_MESSAGE_SEVERITY_ERROR,
            TRUE);
        if (FAILED(corruption_result) || FAILED(error_result)) {
            return core::Result<ComPtr<ID3D12InfoQueue>>::failure(
                graphics_error(
                    core::ErrorCode::operation_failed,
                    "Failed to configure D3D12 debugger breaks"));
        }
    }

    return core::Result<ComPtr<ID3D12InfoQueue>>::success(
        std::move(info_queue));
}

[[nodiscard]] core::Result<ComPtr<IDXGIInfoQueue>>
configure_dxgi_info_queue(const bool debug_layer_enabled)
{
    if (!debug_layer_enabled) {
        return core::Result<ComPtr<IDXGIInfoQueue>>::success({});
    }

    ComPtr<IDXGIInfoQueue> info_queue;
    const auto interface_result = DXGIGetDebugInterface1(
        0,
        IID_PPV_ARGS(&info_queue));
    if (FAILED(interface_result)) {
        return core::Result<ComPtr<IDXGIInfoQueue>>::failure(
            graphics_error(
                core::ErrorCode::unavailable,
                hresult_message(
                    "DXGIGetDebugInterface1(IDXGIInfoQueue)",
                    interface_result)));
    }

    const auto limit_result = info_queue->SetMessageCountLimit(
        DXGI_DEBUG_DXGI,
        debug_message_limit);
    if (FAILED(limit_result)) {
        return core::Result<ComPtr<IDXGIInfoQueue>>::failure(
            graphics_error(
                core::ErrorCode::operation_failed,
                hresult_message(
                    "IDXGIInfoQueue::SetMessageCountLimit",
                    limit_result)));
    }

    if (IsDebuggerPresent() != FALSE) {
        const auto corruption_result = info_queue->SetBreakOnSeverity(
            DXGI_DEBUG_DXGI,
            DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
            TRUE);
        const auto error_result = info_queue->SetBreakOnSeverity(
            DXGI_DEBUG_DXGI,
            DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
            TRUE);
        if (FAILED(corruption_result) || FAILED(error_result)) {
            return core::Result<ComPtr<IDXGIInfoQueue>>::failure(
                graphics_error(
                    core::ErrorCode::operation_failed,
                    "Failed to configure DXGI debugger breaks"));
        }
    }

    return core::Result<ComPtr<IDXGIInfoQueue>>::success(
        std::move(info_queue));
}

[[nodiscard]] std::string dred_name(
    const char* const narrow_name,
    const wchar_t* const wide_name)
{
    if (narrow_name != nullptr && narrow_name[0] != '\0') {
        return narrow_name;
    }
    if (wide_name != nullptr && wide_name[0] != L'\0') {
        return utf8_from_wide(wide_name);
    }
    return "<unnamed>";
}

void log_dred_allocations(
    const std::string_view heading,
    const D3D12_DRED_ALLOCATION_NODE1* node)
{
    std::size_t count = 0;
    for (; node != nullptr && count < maximum_dred_nodes;
         node = node->pNext, ++count) {
        auto message = std::string{heading};
        message.append(": ");
        message.append(dred_name(node->ObjectNameA, node->ObjectNameW));
        message.append(" (type=");
        message.append(std::to_string(
            static_cast<std::uint32_t>(node->AllocationType)));
        message.push_back(')');
        core::log_message(
            core::LogLevel::error,
            "gpu.dred",
            message);
    }
    if (node != nullptr) {
        core::log_message(
            core::LogLevel::warning,
            "gpu.dred",
            std::string{heading} +
                ": additional allocation nodes were truncated");
    }
}

void log_dred_report(ID3D12Device* const device)
{
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const auto interface_result = device->QueryInterface(
        IID_PPV_ARGS(&dred));
    if (FAILED(interface_result)) {
        core::log_message(
            core::LogLevel::error,
            "gpu.dred",
            hresult_message(
                "QueryInterface(ID3D12DeviceRemovedExtendedData1)",
                interface_result));
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    const auto breadcrumb_result = dred->GetAutoBreadcrumbsOutput1(
        &breadcrumbs);
    if (FAILED(breadcrumb_result)) {
        core::log_message(
            core::LogLevel::error,
            "gpu.dred",
            hresult_message("DRED automatic breadcrumbs",
                breadcrumb_result));
    }
    else {
        auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
        std::size_t node_count = 0;
        for (; node != nullptr && node_count < maximum_dred_nodes;
             node = node->pNext, ++node_count) {
            const auto completed = node->pLastBreadcrumbValue != nullptr
                ? *node->pLastBreadcrumbValue
                : 0U;
            auto message = std::string{"queue="};
            message.append(dred_name(
                node->pCommandQueueDebugNameA,
                node->pCommandQueueDebugNameW));
            message.append("; list=");
            message.append(dred_name(
                node->pCommandListDebugNameA,
                node->pCommandListDebugNameW));
            message.append("; completed=");
            message.append(std::to_string(completed));
            message.push_back('/');
            message.append(std::to_string(node->BreadcrumbCount));
            if (node->pCommandHistory != nullptr &&
                completed < node->BreadcrumbCount) {
                const auto history_index =
                    completed % maximum_dred_history_operations;
                message.append("; next-operation=");
                message.append(std::to_string(static_cast<std::uint32_t>(
                    node->pCommandHistory[history_index])));
            }
            core::log_message(
                core::LogLevel::error,
                "gpu.dred",
                message);

            const auto context_count =
                (std::min)(
                    static_cast<std::size_t>(
                        node->BreadcrumbContextsCount),
                    maximum_dred_contexts);
            for (std::size_t index = 0;
                 index < context_count &&
                 node->pBreadcrumbContexts != nullptr;
                 ++index) {
                const auto& context = node->pBreadcrumbContexts[index];
                auto context_message = std::string{"context["};
                context_message.append(std::to_string(
                    context.BreadcrumbIndex));
                context_message.append("]=");
                context_message.append(context.pContextString != nullptr
                    ? utf8_from_wide(context.pContextString)
                    : "<unnamed>");
                core::log_message(
                    core::LogLevel::error,
                    "gpu.dred",
                    context_message);
            }
        }
        if (node != nullptr) {
            core::log_message(
                core::LogLevel::warning,
                "gpu.dred",
                "Additional breadcrumb nodes were truncated");
        }
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 page_fault{};
    const auto page_fault_result = dred->GetPageFaultAllocationOutput1(
        &page_fault);
    if (FAILED(page_fault_result)) {
        core::log_message(
            core::LogLevel::error,
            "gpu.dred",
            hresult_message("DRED page-fault allocations",
                page_fault_result));
        return;
    }
    core::log_message(
        core::LogLevel::error,
        "gpu.dred",
        std::string{"page-fault-address="} +
            hexadecimal(page_fault.PageFaultVA, 16));
    log_dred_allocations(
        "existing allocation",
        page_fault.pHeadExistingAllocationNode);
    log_dred_allocations(
        "recently freed allocation",
        page_fault.pHeadRecentFreedAllocationNode);
}

} // namespace

class Device::Implementation final {
public:
    UniqueModule packaged_warp;
    ComPtr<IDXGIFactory7> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> info_queue;
    ComPtr<IDXGIInfoQueue> dxgi_info_queue;
    std::vector<AdapterInfo> adapter_candidates;
    AdapterInfo selected_adapter;
    RendererCaps capabilities;
    DebugMessageCounts debug_message_counts;
    UINT64 debug_message_cursor{};
    UINT64 discarded_debug_messages{};
    UINT64 dxgi_message_cursor{};
    UINT64 discarded_dxgi_messages{};
    std::string agility_runtime_path;
    std::optional<std::string> warp_runtime_path;
    bool debug_layer_enabled{};
    bool gpu_based_validation_enabled{};
    bool dred_enabled{};
};

AdapterSelection AdapterSelection::high_performance() noexcept
{
    return AdapterSelection{AdapterSelectionKind::high_performance, 0};
}

AdapterSelection AdapterSelection::by_preference_index(
    const std::uint32_t index) noexcept
{
    return AdapterSelection{AdapterSelectionKind::preference_index, index};
}

AdapterSelection AdapterSelection::warp() noexcept
{
    return AdapterSelection{AdapterSelectionKind::warp, 0};
}

bool supports_required_baseline(const AdapterInfo& adapter) noexcept
{
    return adapter.supports_feature_level_12_0 &&
        adapter.highest_shader_model.has_value() &&
        shader_model_at_least(
            *adapter.highest_shader_model,
            required_shader_model);
}

Device::Device(std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation))
{
}

Device::Device(Device&&) noexcept = default;
Device& Device::operator=(Device&&) noexcept = default;
Device::~Device() = default;

core::Result<Device> Device::create(const DeviceConfig& config)
{
    if (config.enable_gpu_based_validation &&
        !config.enable_debug_layer) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::invalid_argument,
            "GPU-based validation requires the D3D12 debug layer"));
    }

    if (device_creation_started.test_and_set(std::memory_order_acq_rel)) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "Shark permits one authoritative D3D12 device startup per "
            "process"));
    }

    auto export_result = verify_agility_exports();
    if (!export_result) {
        return core::Result<Device>::failure(
            std::move(export_result).error());
    }

    auto implementation = std::make_unique<Implementation>();
    if (config.adapter.kind == AdapterSelectionKind::warp) {
        auto warp_result = load_packaged_warp();
        if (!warp_result) {
            return core::Result<Device>::failure(
                std::move(warp_result).error());
        }
        auto loaded_warp = std::move(warp_result).value();
        implementation->warp_runtime_path = std::move(loaded_warp.path);
        implementation->packaged_warp = std::move(loaded_warp.module);
        core::log_message(
            core::LogLevel::info,
            "gpu.runtime",
            std::string{"Loaded packaged WARP: "} +
                *implementation->warp_runtime_path);
    }

    auto debug_result = configure_debug_layer(
        config.enable_debug_layer,
        config.enable_gpu_based_validation);
    if (!debug_result) {
        return core::Result<Device>::failure(
            std::move(debug_result).error());
    }
    implementation->debug_layer_enabled = config.enable_debug_layer;
    implementation->gpu_based_validation_enabled =
        config.enable_gpu_based_validation;

    auto dred_result = configure_dred();
    if (!dred_result) {
        return core::Result<Device>::failure(
            std::move(dred_result).error());
    }
    implementation->dred_enabled = true;

    auto dxgi_info_queue_result = configure_dxgi_info_queue(
        config.enable_debug_layer);
    if (!dxgi_info_queue_result) {
        return core::Result<Device>::failure(
            std::move(dxgi_info_queue_result).error());
    }
    auto dxgi_info_queue = std::move(dxgi_info_queue_result).value();

    auto runtime_path_result = loaded_agility_runtime_path();
    if (!runtime_path_result) {
        return core::Result<Device>::failure(
            std::move(runtime_path_result).error());
    }
    implementation->agility_runtime_path =
        std::move(runtime_path_result).value();
    core::log_message(
        core::LogLevel::info,
        "gpu.runtime",
        std::string{"DirectX 12 Agility SDK "} +
            std::to_string(D3D12_SDK_VERSION) + " runtime: " +
            implementation->agility_runtime_path);

    UINT factory_flags = 0;
    if (config.enable_debug_layer) {
        factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    ComPtr<IDXGIFactory7> factory;
    const auto factory_result = CreateDXGIFactory2(
        factory_flags,
        IID_PPV_ARGS(&factory));
    if (FAILED(factory_result)) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("CreateDXGIFactory2", factory_result)));
    }

    auto enumeration_result = enumerate_adapters(factory.Get());
    if (!enumeration_result) {
        return core::Result<Device>::failure(
            std::move(enumeration_result).error());
    }
    auto candidates = std::move(enumeration_result).value();
    implementation->adapter_candidates.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        implementation->adapter_candidates.push_back(candidate.info);
        log_adapter(candidate.info);
    }

    core::Result<Candidate> selection_result =
        config.adapter.kind == AdapterSelectionKind::warp
        ? select_warp_candidate(factory.Get())
        : select_hardware_candidate(config.adapter, candidates);
    if (!selection_result) {
        return core::Result<Device>::failure(
            std::move(selection_result).error());
    }
    auto selected = std::move(selection_result).value();
    if (config.adapter.kind == AdapterSelectionKind::warp) {
        log_adapter(selected.info);
    }

    ComPtr<ID3D12Device> device;
    const auto device_result = D3D12CreateDevice(
        selected.adapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device));
    if (FAILED(device_result)) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("D3D12CreateDevice", device_result)));
    }
    const auto name_result = device->SetName(L"Shark D3D12 Device");
    if (FAILED(name_result)) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message("ID3D12Object::SetName", name_result)));
    }

    auto info_queue_result = configure_info_queue(
        device.Get(),
        config.enable_debug_layer);
    if (!info_queue_result) {
        return core::Result<Device>::failure(
            std::move(info_queue_result).error());
    }
    auto info_queue = std::move(info_queue_result).value();

    auto capabilities_result = query_capabilities(device.Get());
    if (!capabilities_result) {
        return core::Result<Device>::failure(
            std::move(capabilities_result).error());
    }
    auto capabilities = std::move(capabilities_result).value();
    if (!shader_model_at_least(
            capabilities.highest_shader_model,
            required_shader_model)) {
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::unsupported,
            "The selected adapter does not support Shader Model 6.0"));
    }

    auto messages_result = inspect_debug_messages(info_queue.Get());
    if (!messages_result) {
        return core::Result<Device>::failure(
            std::move(messages_result).error());
    }
    auto message_counts = std::move(messages_result).value();
    auto dxgi_messages_result = inspect_dxgi_messages(
        dxgi_info_queue.Get());
    if (!dxgi_messages_result) {
        return core::Result<Device>::failure(
            std::move(dxgi_messages_result).error());
    }
    accumulate_message_counts(
        message_counts,
        dxgi_messages_result.value());
    if (message_counts.corruption != 0 || message_counts.error != 0) {
        auto message = std::string{
            "DirectX initialization produced validation failures: "};
        message.append(std::to_string(message_counts.corruption));
        message.append(" corruption, ");
        message.append(std::to_string(message_counts.error));
        message.append(" error");
        return core::Result<Device>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            std::move(message)));
    }

    implementation->factory = std::move(factory);
    implementation->adapter = std::move(selected.adapter);
    implementation->selected_adapter = std::move(selected.info);
    implementation->device = std::move(device);
    implementation->info_queue = std::move(info_queue);
    implementation->dxgi_info_queue = std::move(dxgi_info_queue);
    implementation->capabilities = std::move(capabilities);
    implementation->debug_message_counts = message_counts;
    if (implementation->info_queue != nullptr) {
        implementation->debug_message_cursor =
            implementation->info_queue->GetNumStoredMessages();
        implementation->discarded_debug_messages =
            implementation->info_queue->
                GetNumMessagesDiscardedByMessageCountLimit();
    }
    if (implementation->dxgi_info_queue != nullptr) {
        implementation->dxgi_message_cursor =
            implementation->dxgi_info_queue->GetNumStoredMessages(
                DXGI_DEBUG_DXGI);
        implementation->discarded_dxgi_messages =
            implementation->dxgi_info_queue->
                GetNumMessagesDiscardedByMessageCountLimit(
                    DXGI_DEBUG_DXGI);
    }

    core::log_message(
        core::LogLevel::info,
        "gpu.device",
        std::string{"Selected adapter: "} +
            implementation->selected_adapter.name);
    core::log_message(
        core::LogLevel::info,
        "gpu.device",
        std::string{"Debug layer="} +
            (implementation->debug_layer_enabled ? "enabled" : "disabled") +
            "; GPU validation=" +
            (implementation->gpu_based_validation_enabled
                ? "enabled"
                : "disabled") +
            "; DRED=enabled");
    log_capabilities(implementation->capabilities);
    core::log_message(
        core::LogLevel::info,
        "gpu.validation",
        std::string{"Initialization messages: 0 corruption, 0 errors, "} +
            std::to_string(message_counts.warning) + " warnings");

    return core::Result<Device>::success(
        Device{std::move(implementation)});
}

std::span<const AdapterInfo> Device::adapter_candidates() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no adapter report");
    return implementation_->adapter_candidates;
}

const AdapterInfo& Device::selected_adapter() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no selected adapter");
    return implementation_->selected_adapter;
}

const RendererCaps& Device::capabilities() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no capabilities");
    return implementation_->capabilities;
}

const DebugMessageCounts& Device::debug_message_counts() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no validation report");
    return implementation_->debug_message_counts;
}

bool Device::debug_layer_enabled() const noexcept
{
    return implementation_ != nullptr &&
        implementation_->debug_layer_enabled;
}

bool Device::gpu_based_validation_enabled() const noexcept
{
    return implementation_ != nullptr &&
        implementation_->gpu_based_validation_enabled;
}

bool Device::dred_enabled() const noexcept
{
    return implementation_ != nullptr && implementation_->dred_enabled;
}

std::uint32_t Device::agility_sdk_version() const noexcept
{
    return D3D12_SDK_VERSION;
}

std::string_view Device::agility_runtime_path() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no runtime report");
    return implementation_->agility_runtime_path;
}

const std::optional<std::string>& Device::warp_runtime_path() const noexcept
{
    SHARK_ENSURE(
        implementation_ != nullptr,
        "A moved-from D3D12 Device has no WARP runtime report");
    return implementation_->warp_runtime_path;
}

core::Result<void> Device::validate_debug_state()
{
    if (implementation_ == nullptr) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::invalid_state,
            "A moved-from D3D12 Device cannot be validated"));
    }
    if (!implementation_->debug_layer_enabled) {
        return core::Result<void>::success();
    }

    ComPtr<ID3D12DebugDevice2> debug_device;
    const auto interface_result = implementation_->device.As(&debug_device);
    if (FAILED(interface_result)) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::unavailable,
            hresult_message("QueryInterface(ID3D12DebugDevice2)",
                interface_result)));
    }
    const auto report_result = debug_device->ReportLiveDeviceObjects(
        static_cast<D3D12_RLDO_FLAGS>(
            D3D12_RLDO_SUMMARY |
            D3D12_RLDO_DETAIL |
            D3D12_RLDO_IGNORE_INTERNAL));
    if (FAILED(report_result)) {
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            hresult_message(
                "ID3D12DebugDevice2::ReportLiveDeviceObjects",
                report_result)));
    }

    const auto discarded = implementation_->info_queue->
        GetNumMessagesDiscardedByMessageCountLimit();
    const auto discarded_new_messages =
        discarded > implementation_->discarded_debug_messages;
    const auto discarded_dxgi = implementation_->dxgi_info_queue->
        GetNumMessagesDiscardedByMessageCountLimit(DXGI_DEBUG_DXGI);
    const auto discarded_new_dxgi_messages =
        discarded_dxgi > implementation_->discarded_dxgi_messages;

    std::uint64_t live_child_count = 0;
    auto messages_result = inspect_debug_messages(
        implementation_->info_queue.Get(),
        implementation_->debug_message_cursor,
        &live_child_count);
    if (!messages_result) {
        return core::Result<void>::failure(
            std::move(messages_result).error());
    }
    auto message_counts = std::move(messages_result).value();
    auto dxgi_messages_result = inspect_dxgi_messages(
        implementation_->dxgi_info_queue.Get(),
        implementation_->dxgi_message_cursor);
    if (!dxgi_messages_result) {
        return core::Result<void>::failure(
            std::move(dxgi_messages_result).error());
    }
    accumulate_message_counts(
        message_counts,
        dxgi_messages_result.value());
    implementation_->debug_message_cursor =
        implementation_->info_queue->GetNumStoredMessages();
    implementation_->discarded_debug_messages = discarded;
    implementation_->dxgi_message_cursor =
        implementation_->dxgi_info_queue->GetNumStoredMessages(
            DXGI_DEBUG_DXGI);
    implementation_->discarded_dxgi_messages = discarded_dxgi;
    accumulate_message_counts(
        implementation_->debug_message_counts,
        message_counts);

    if (discarded_new_messages ||
        discarded_new_dxgi_messages ||
        message_counts.corruption != 0 ||
        message_counts.error != 0 ||
        live_child_count != 0) {
        auto message = std::string{"DirectX end-of-run validation failed: "};
        message.append(std::to_string(message_counts.corruption));
        message.append(" corruption, ");
        message.append(std::to_string(message_counts.error));
        message.append(" errors, ");
        message.append(std::to_string(live_child_count));
        message.append(" live D3D12 child objects");
        if (discarded_new_messages || discarded_new_dxgi_messages) {
            message.append(
                ", and a bounded validation queue discarded messages");
        }
        return core::Result<void>::failure(graphics_error(
            core::ErrorCode::operation_failed,
            std::move(message)));
    }

    core::log_message(
        core::LogLevel::info,
        "gpu.validation",
        std::string{"DirectX end-of-run validation: 0 corruption, 0 "
            "errors, 0 "
            "live D3D12 child objects, "} +
            std::to_string(message_counts.warning) + " warnings");
    return core::Result<void>::success();
}

detail::NativeDeviceContext detail::DeviceAccess::native_context(
    Device& device) noexcept
{
    if (device.implementation_ == nullptr) {
        return {};
    }
    return detail::NativeDeviceContext{
        device.implementation_->device.Get(),
        device.implementation_->factory.Get(),
    };
}

core::Error detail::DeviceAccess::graphics_failure(
    Device& device,
    const std::string_view operation,
    const HRESULT result)
{
    auto message = hresult_message(operation, result);
    if (device.implementation_ == nullptr ||
        device.implementation_->device == nullptr) {
        return graphics_error(
            core::ErrorCode::operation_failed,
            std::move(message));
    }

    const auto removal_reason =
        device.implementation_->device->GetDeviceRemovedReason();
    const auto removal_path =
        result == DXGI_ERROR_DEVICE_REMOVED ||
        result == DXGI_ERROR_DEVICE_RESET ||
        result == DXGI_ERROR_DEVICE_HUNG ||
        result == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
        FAILED(removal_reason);
    if (removal_path) {
        message.append("; device removal reason ");
        message.append(hexadecimal(
            static_cast<std::uint32_t>(removal_reason),
            8));
        core::log_message(
            core::LogLevel::critical,
            "gpu.device",
            message);
        log_dred_report(device.implementation_->device.Get());
    }
    return graphics_error(
        core::ErrorCode::operation_failed,
        std::move(message));
}

} // namespace shark::rhi::d3d12
