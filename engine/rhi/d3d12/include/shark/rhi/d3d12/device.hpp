#pragma once

#include <shark/core/result.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace shark::rhi::d3d12 {

enum class AdapterSelectionKind : std::uint8_t {
    high_performance = 1,
    preference_index,
    warp,
};

struct AdapterSelection final {
    AdapterSelectionKind kind{AdapterSelectionKind::high_performance};
    std::uint32_t preference_index{};

    [[nodiscard]] static AdapterSelection high_performance() noexcept;
    [[nodiscard]] static AdapterSelection by_preference_index(
        std::uint32_t index) noexcept;
    [[nodiscard]] static AdapterSelection warp() noexcept;
};

struct ShaderModel final {
    std::uint16_t major{};
    std::uint16_t minor{};

    [[nodiscard]] friend bool operator==(
        const ShaderModel&,
        const ShaderModel&) noexcept = default;
};

struct TierVersion final {
    std::uint16_t major{};
    std::uint16_t minor{};

    [[nodiscard]] friend bool operator==(
        const TierVersion&,
        const TierVersion&) noexcept = default;
};

struct AdapterLuid final {
    std::uint32_t low_part{};
    std::int32_t high_part{};

    [[nodiscard]] friend bool operator==(
        const AdapterLuid&,
        const AdapterLuid&) noexcept = default;
};

struct AdapterInfo final {
    std::optional<std::uint32_t> preference_index;
    std::string name;
    AdapterLuid luid;
    std::uint32_t vendor_id{};
    std::uint32_t device_id{};
    std::uint32_t subsystem_id{};
    std::uint32_t revision{};
    std::uint64_t dedicated_video_memory{};
    std::uint64_t dedicated_system_memory{};
    std::uint64_t shared_system_memory{};
    bool software{};
    bool remote{};
    bool supports_feature_level_12_0{};
    std::optional<ShaderModel> highest_shader_model;
    std::string rejection_reason;
};

struct RendererCaps final {
    std::uint16_t maximum_feature_level_major{};
    std::uint16_t maximum_feature_level_minor{};
    ShaderModel highest_shader_model;
    std::uint32_t resource_binding_tier{};
    std::uint32_t resource_heap_tier{};
    std::optional<bool> enhanced_barriers;
    std::optional<TierVersion> raytracing_tier;
    std::optional<TierVersion> mesh_shader_tier;
    std::optional<std::uint32_t> variable_shading_rate_tier;
    std::optional<TierVersion> sampler_feedback_tier;
    std::optional<TierVersion> work_graphs_tier;
    std::optional<bool> unified_memory_architecture;
    std::optional<bool> cache_coherent_unified_memory_architecture;
};

struct DebugMessageCounts final {
    std::uint64_t corruption{};
    std::uint64_t error{};
    std::uint64_t warning{};
    std::uint64_t info{};
    std::uint64_t message{};
};

struct DeviceConfig final {
    AdapterSelection adapter{AdapterSelection::high_performance()};
    bool enable_debug_layer{true};
    bool enable_gpu_based_validation{};
};

[[nodiscard]] bool supports_required_baseline(
    const AdapterInfo& adapter) noexcept;

class Device final {
public:
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    ~Device();

    [[nodiscard]] static core::Result<Device> create(
        const DeviceConfig& config = {});

    [[nodiscard]] std::span<const AdapterInfo> adapter_candidates()
        const noexcept;
    [[nodiscard]] const AdapterInfo& selected_adapter() const noexcept;
    [[nodiscard]] const RendererCaps& capabilities() const noexcept;
    [[nodiscard]] const DebugMessageCounts& debug_message_counts()
        const noexcept;
    [[nodiscard]] bool debug_layer_enabled() const noexcept;
    [[nodiscard]] bool gpu_based_validation_enabled() const noexcept;
    [[nodiscard]] bool dred_enabled() const noexcept;
    [[nodiscard]] std::uint32_t agility_sdk_version() const noexcept;
    [[nodiscard]] std::string_view agility_runtime_path() const noexcept;
    [[nodiscard]] const std::optional<std::string>& warp_runtime_path()
        const noexcept;

private:
    class Implementation;

    explicit Device(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

} // namespace shark::rhi::d3d12
