#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shark::terrain {

inline constexpr std::uint32_t material_texture_width = 32;
inline constexpr std::uint32_t material_texture_height = 32;
inline constexpr std::uint32_t material_layer_count = 2;
inline constexpr std::uint32_t material_texture_mip_levels = 6;
inline constexpr std::size_t material_texel_bytes = 4;

[[nodiscard]] constexpr std::uint32_t material_mip_width(
    const std::uint32_t mip) noexcept
{
    return mip < material_texture_mip_levels
        ? material_texture_width >> mip
        : 0;
}

[[nodiscard]] constexpr std::uint32_t material_mip_height(
    const std::uint32_t mip) noexcept
{
    return mip < material_texture_mip_levels
        ? material_texture_height >> mip
        : 0;
}

[[nodiscard]] constexpr std::size_t material_mip_source_bytes(
    const std::uint32_t mip) noexcept
{
    return static_cast<std::size_t>(material_mip_width(mip)) *
        static_cast<std::size_t>(material_mip_height(mip)) *
        material_texel_bytes;
}

inline constexpr std::size_t material_layer_source_bytes = [] {
    std::size_t result = 0;
    for (std::uint32_t mip = 0;
         mip < material_texture_mip_levels;
         ++mip) {
        result += material_mip_source_bytes(mip);
    }
    return result;
}();
inline constexpr std::size_t material_array_source_bytes =
    material_layer_source_bytes *
    static_cast<std::size_t>(material_layer_count);
inline constexpr std::size_t material_total_source_bytes =
    material_array_source_bytes * 3U;
inline constexpr std::size_t material_subresources_per_array =
    static_cast<std::size_t>(material_layer_count) *
    material_texture_mip_levels;
inline constexpr std::size_t material_total_subresources =
    material_subresources_per_array * 3U;

[[nodiscard]] constexpr std::size_t material_subresource_offset(
    const std::uint32_t layer,
    const std::uint32_t mip) noexcept
{
    if (layer >= material_layer_count ||
        mip >= material_texture_mip_levels) {
        return material_array_source_bytes;
    }
    auto result =
        static_cast<std::size_t>(layer) *
        material_layer_source_bytes;
    for (std::uint32_t preceding = 0;
         preceding < mip;
         ++preceding) {
        result += material_mip_source_bytes(preceding);
    }
    return result;
}

static_assert(material_layer_source_bytes == 5'460);
static_assert(material_array_source_bytes == 10'920);
static_assert(material_total_source_bytes == 32'760);
static_assert(material_subresources_per_array == 12);
static_assert(material_total_subresources == 36);

enum class MaterialLayer : std::uint8_t {
    ground = 0,
    rock,
};

// Project-owned deterministic T-003 test content. Albedo bytes are authored
// for sRGB decoding. Normal and roughness bytes are linear RGBA8 data.
struct MaterialPalette final {
    std::array<std::byte, material_array_source_bytes> albedo;
    std::array<std::byte, material_array_source_bytes> normal;
    std::array<std::byte, material_array_source_bytes> roughness;

    [[nodiscard]] friend bool operator==(
        const MaterialPalette&,
        const MaterialPalette&) = default;
};

[[nodiscard]] MaterialPalette make_deterministic_material_palette()
    noexcept;

} // namespace shark::terrain
