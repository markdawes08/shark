#include <shark/terrain/material_palette.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

using MaterialBytes = std::array<
    std::byte,
    shark::terrain::material_array_source_bytes>;

[[nodiscard]] constexpr std::uint8_t load_channel(
    const MaterialBytes& source,
    const std::uint32_t layer,
    const std::uint32_t mip,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::size_t channel) noexcept
{
    const auto texel =
        static_cast<std::size_t>(y) *
            shark::terrain::material_mip_width(mip) +
        static_cast<std::size_t>(x);
    const auto offset =
        shark::terrain::material_subresource_offset(layer, mip) +
        texel * shark::terrain::material_texel_bytes;
    return std::to_integer<std::uint8_t>(source[offset + channel]);
}

[[nodiscard]] constexpr float decode_normal_channel(
    const std::uint8_t encoded) noexcept
{
    return static_cast<float>(encoded) *
        (2.0F / 255.0F) - 1.0F;
}

} // namespace

TEST_CASE(
    "terrain material palette has an exact deterministic fixture",
    "[terrain][material][material-palette][contract]")
{
    using namespace shark::terrain;

    STATIC_REQUIRE(material_layer_count == 2);
    STATIC_REQUIRE(material_texture_width == 32);
    STATIC_REQUIRE(material_texture_height == 32);
    STATIC_REQUIRE(material_texture_mip_levels == 6);
    STATIC_REQUIRE(material_texel_bytes == 4);
    STATIC_REQUIRE(material_subresources_per_array == 12);
    STATIC_REQUIRE(material_total_subresources == 36);
    STATIC_REQUIRE(material_layer_source_bytes == 5'460);
    STATIC_REQUIRE(material_array_source_bytes == 10'920);
    STATIC_REQUIRE(material_total_source_bytes == 32'760);
    STATIC_REQUIRE(
        static_cast<std::uint8_t>(MaterialLayer::ground) == 0);
    STATIC_REQUIRE(
        static_cast<std::uint8_t>(MaterialLayer::rock) == 1);

    const auto first = make_deterministic_material_palette();
    const auto second = make_deterministic_material_palette();

    REQUIRE(first == second);
    REQUIRE(first.albedo.size() == material_array_source_bytes);
    REQUIRE(first.normal.size() == material_array_source_bytes);
    REQUIRE(first.roughness.size() == material_array_source_bytes);
    REQUIRE(
        first.albedo.size() +
            first.normal.size() +
            first.roughness.size() ==
        material_total_source_bytes);
}

TEST_CASE(
    "terrain material subresources are layer-major and mip-minor",
    "[terrain][material][material-palette][layout]")
{
    using namespace shark::terrain;

    constexpr std::array<std::uint32_t, material_texture_mip_levels>
        expected_dimensions{{32, 16, 8, 4, 2, 1}};
    constexpr std::array<std::size_t, material_texture_mip_levels>
        expected_mip_bytes{{4'096, 1'024, 256, 64, 16, 4}};
    constexpr std::array<
        std::array<std::size_t, material_texture_mip_levels>,
        material_layer_count>
        expected_offsets{{
            {{0, 4'096, 5'120, 5'376, 5'440, 5'456}},
            {{5'460, 9'556, 10'580, 10'836, 10'900, 10'916}},
        }};

    std::size_t cursor = 0;
    for (std::uint32_t layer = 0;
         layer < material_layer_count;
         ++layer) {
        for (std::uint32_t mip = 0;
             mip < material_texture_mip_levels;
             ++mip) {
            REQUIRE(material_mip_width(mip) ==
                expected_dimensions[mip]);
            REQUIRE(material_mip_height(mip) ==
                expected_dimensions[mip]);
            REQUIRE(material_mip_source_bytes(mip) ==
                expected_mip_bytes[mip]);
            REQUIRE(material_subresource_offset(layer, mip) ==
                expected_offsets[layer][mip]);
            REQUIRE(material_subresource_offset(layer, mip) ==
                cursor);
            cursor += material_mip_source_bytes(mip);
        }
    }
    REQUIRE(cursor == material_array_source_bytes);
    STATIC_REQUIRE(
        material_mip_width(material_texture_mip_levels) == 0);
    STATIC_REQUIRE(
        material_mip_height(material_texture_mip_levels) == 0);
    STATIC_REQUIRE(
        material_mip_source_bytes(material_texture_mip_levels) == 0);
    STATIC_REQUIRE(
        material_subresource_offset(
            material_layer_count,
            0) == material_array_source_bytes);
    STATIC_REQUIRE(
        material_subresource_offset(
            0,
            material_texture_mip_levels) ==
        material_array_source_bytes);
}

TEST_CASE(
    "terrain albedo stays opaque and normal mip texels stay normalized",
    "[terrain][material][material-palette][texels]")
{
    using namespace shark::terrain;

    const auto palette = make_deterministic_material_palette();
    const auto normal_length_tolerance =
        std::sqrt(3.0F) / 255.0F + 0.000001F;

    for (std::uint32_t layer = 0;
         layer < material_layer_count;
         ++layer) {
        for (std::uint32_t mip = 0;
             mip < material_texture_mip_levels;
             ++mip) {
            for (std::uint32_t y = 0;
                 y < material_mip_height(mip);
                 ++y) {
                for (std::uint32_t x = 0;
                     x < material_mip_width(mip);
                     ++x) {
                    REQUIRE(load_channel(
                        palette.albedo,
                        layer,
                        mip,
                        x,
                        y,
                        3) == 255);

                    const auto normal_x = decode_normal_channel(
                        load_channel(
                            palette.normal,
                            layer,
                            mip,
                            x,
                            y,
                            0));
                    const auto normal_y = decode_normal_channel(
                        load_channel(
                            palette.normal,
                            layer,
                            mip,
                            x,
                            y,
                            1));
                    const auto normal_z = decode_normal_channel(
                        load_channel(
                            palette.normal,
                            layer,
                            mip,
                            x,
                            y,
                            2));
                    const auto normal_length = std::sqrt(
                        normal_x * normal_x +
                        normal_y * normal_y +
                        normal_z * normal_z);

                    REQUIRE(normal_length ==
                        Catch::Approx(1.0F).margin(
                            normal_length_tolerance));
                }
            }
        }
    }
}

TEST_CASE(
    "terrain roughness uses linear scalar channels at every mip",
    "[terrain][material][material-palette][texels]")
{
    using namespace shark::terrain;

    const auto palette = make_deterministic_material_palette();

    for (std::uint32_t layer = 0;
         layer < material_layer_count;
         ++layer) {
        for (std::uint32_t mip = 0;
             mip < material_texture_mip_levels;
             ++mip) {
            for (std::uint32_t y = 0;
                 y < material_mip_height(mip);
                 ++y) {
                for (std::uint32_t x = 0;
                     x < material_mip_width(mip);
                     ++x) {
                    const auto roughness = load_channel(
                        palette.roughness,
                        layer,
                        mip,
                        x,
                        y,
                        0);
                    REQUIRE(load_channel(
                        palette.roughness,
                        layer,
                        mip,
                        x,
                        y,
                        1) == roughness);
                    REQUIRE(load_channel(
                        palette.roughness,
                        layer,
                        mip,
                        x,
                        y,
                        2) == roughness);
                    REQUIRE(load_channel(
                        palette.roughness,
                        layer,
                        mip,
                        x,
                        y,
                        3) == 255);

                    if (mip == 0) {
                        continue;
                    }

                    std::uint32_t source_sum = 0;
                    for (std::uint32_t source_y = 0;
                         source_y < 2;
                         ++source_y) {
                        for (std::uint32_t source_x = 0;
                             source_x < 2;
                             ++source_x) {
                            source_sum += load_channel(
                                palette.roughness,
                                layer,
                                mip - 1U,
                                x * 2U + source_x,
                                y * 2U + source_y,
                                0);
                        }
                    }
                    REQUIRE(roughness ==
                        static_cast<std::uint8_t>(
                            (source_sum + 2U) / 4U));
                }
            }
        }
    }
}
