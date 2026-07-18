#include <shark/terrain/material_palette.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace shark::terrain {
namespace {

[[nodiscard]] constexpr std::uint32_t texel_hash(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t layer) noexcept
{
    auto value =
        x * 0x9E3779B9U ^
        y * 0x85EBCA6BU ^
        (layer + 1U) * 0xC2B2AE35U;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] constexpr std::uint8_t bounded_byte(
    const int value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

constexpr void store_texel(
    std::array<std::byte, material_array_source_bytes>& destination,
    const std::uint32_t layer,
    const std::uint32_t mip,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::array<std::uint8_t, 4> value) noexcept
{
    const auto texel =
        static_cast<std::size_t>(y) * material_mip_width(mip) +
        static_cast<std::size_t>(x);
    const auto offset =
        material_subresource_offset(layer, mip) +
        texel * material_texel_bytes;
    for (std::size_t channel = 0; channel < value.size(); ++channel) {
        destination[offset + channel] =
            static_cast<std::byte>(value[channel]);
    }
}

[[nodiscard]] constexpr std::uint8_t load_channel(
    const std::array<std::byte, material_array_source_bytes>& source,
    const std::uint32_t layer,
    const std::uint32_t mip,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::size_t channel) noexcept
{
    const auto texel =
        static_cast<std::size_t>(y) * material_mip_width(mip) +
        static_cast<std::size_t>(x);
    const auto offset =
        material_subresource_offset(layer, mip) +
        texel * material_texel_bytes;
    return std::to_integer<std::uint8_t>(source[offset + channel]);
}

[[nodiscard]] float srgb_to_linear(const std::uint8_t encoded) noexcept
{
    const auto value = static_cast<float>(encoded) / 255.0F;
    return value <= 0.04045F
        ? value / 12.92F
        : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] std::uint8_t linear_to_srgb(const float linear) noexcept
{
    const auto value = std::clamp(linear, 0.0F, 1.0F);
    const auto encoded = value <= 0.0031308F
        ? value * 12.92F
        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
    return bounded_byte(static_cast<int>(
        std::lround(encoded * 255.0F)));
}

[[nodiscard]] constexpr float decode_normal_channel(
    const std::uint8_t encoded) noexcept
{
    return static_cast<float>(encoded) *
        (2.0F / 255.0F) - 1.0F;
}

[[nodiscard]] std::uint8_t encode_normal_channel(
    const float value) noexcept
{
    return bounded_byte(static_cast<int>(std::lround(
        (std::clamp(value, -1.0F, 1.0F) * 0.5F + 0.5F) *
        255.0F)));
}

} // namespace

MaterialPalette make_deterministic_material_palette() noexcept
{
    MaterialPalette palette{};
    constexpr std::array<std::array<int, 3>, material_layer_count>
        base_albedo{{
            {{72, 112, 48}},
            {{118, 108, 91}},
        }};
    constexpr std::array<int, material_layer_count>
        normal_amplitude{{11, 28}};
    constexpr std::array<int, material_layer_count>
        base_roughness{{226, 174}};

    for (std::uint32_t layer = 0;
         layer < material_layer_count;
         ++layer) {
        for (std::uint32_t y = 0;
             y < material_texture_height;
             ++y) {
            for (std::uint32_t x = 0;
                 x < material_texture_width;
                 ++x) {
                const auto hash = texel_hash(x, y, layer);
                const auto variation =
                    static_cast<int>(hash & 31U) - 15;
                store_texel(
                    palette.albedo,
                    layer,
                    0,
                    x,
                    y,
                    {{
                        bounded_byte(
                            base_albedo[layer][0] + variation),
                        bounded_byte(
                            base_albedo[layer][1] +
                            variation / 2),
                        bounded_byte(
                            base_albedo[layer][2] +
                            variation / 3),
                        255,
                    }});

                const auto signed_x =
                    static_cast<int>((hash >> 8U) & 31U) - 15;
                const auto signed_y =
                    static_cast<int>((hash >> 13U) & 31U) - 15;
                const auto normal_x =
                    static_cast<float>(
                        signed_x * normal_amplitude[layer]) /
                    (15.0F * 127.0F);
                const auto normal_y =
                    static_cast<float>(
                        signed_y * normal_amplitude[layer]) /
                    (15.0F * 127.0F);
                const auto normal_z = std::sqrt(std::max(
                    0.0F,
                    1.0F -
                        normal_x * normal_x -
                        normal_y * normal_y));
                store_texel(
                    palette.normal,
                    layer,
                    0,
                    x,
                    y,
                    {{
                        encode_normal_channel(normal_x),
                        encode_normal_channel(normal_y),
                        encode_normal_channel(normal_z),
                        255,
                    }});

                const auto roughness_variation =
                    static_cast<int>((hash >> 18U) & 15U) - 7;
                const auto roughness = bounded_byte(
                    base_roughness[layer] + roughness_variation);
                store_texel(
                    palette.roughness,
                    layer,
                    0,
                    x,
                    y,
                    {{roughness, roughness, roughness, 255}});
            }
        }

        for (std::uint32_t mip = 1;
             mip < material_texture_mip_levels;
             ++mip) {
            const auto preceding = mip - 1U;
            for (std::uint32_t y = 0;
                 y < material_mip_height(mip);
                 ++y) {
                for (std::uint32_t x = 0;
                     x < material_mip_width(mip);
                     ++x) {
                    std::array<float, 3> linear_albedo{};
                    std::array<float, 3> normal{};
                    std::uint32_t roughness_sum = 0;
                    for (std::uint32_t source_y = 0;
                         source_y < 2;
                         ++source_y) {
                        for (std::uint32_t source_x = 0;
                             source_x < 2;
                             ++source_x) {
                            const auto previous_x = x * 2U + source_x;
                            const auto previous_y = y * 2U + source_y;
                            for (std::size_t channel = 0;
                                 channel < linear_albedo.size();
                                 ++channel) {
                                linear_albedo[channel] += srgb_to_linear(
                                    load_channel(
                                        palette.albedo,
                                        layer,
                                        preceding,
                                        previous_x,
                                        previous_y,
                                        channel));
                                normal[channel] += decode_normal_channel(
                                    load_channel(
                                        palette.normal,
                                        layer,
                                        preceding,
                                        previous_x,
                                        previous_y,
                                        channel));
                            }
                            roughness_sum += load_channel(
                                palette.roughness,
                                layer,
                                preceding,
                                previous_x,
                                previous_y,
                                0);
                        }
                    }
                    for (auto& channel : linear_albedo) {
                        channel *= 0.25F;
                    }
                    const auto normal_length = std::sqrt(
                        normal[0] * normal[0] +
                        normal[1] * normal[1] +
                        normal[2] * normal[2]);
                    if (normal_length > 0.0001F) {
                        for (auto& channel : normal) {
                            channel /= normal_length;
                        }
                    }
                    else {
                        normal = {{0.0F, 0.0F, 1.0F}};
                    }
                    store_texel(
                        palette.albedo,
                        layer,
                        mip,
                        x,
                        y,
                        {{
                            linear_to_srgb(linear_albedo[0]),
                            linear_to_srgb(linear_albedo[1]),
                            linear_to_srgb(linear_albedo[2]),
                            255,
                        }});
                    store_texel(
                        palette.normal,
                        layer,
                        mip,
                        x,
                        y,
                        {{
                            encode_normal_channel(normal[0]),
                            encode_normal_channel(normal[1]),
                            encode_normal_channel(normal[2]),
                            255,
                        }});
                    const auto roughness = static_cast<std::uint8_t>(
                        (roughness_sum + 2U) / 4U);
                    store_texel(
                        palette.roughness,
                        layer,
                        mip,
                        x,
                        y,
                        {{roughness, roughness, roughness, 255}});
                }
            }
        }
    }
    return palette;
}

} // namespace shark::terrain
