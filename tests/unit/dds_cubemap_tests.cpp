#include <shark/assets/dds_cubemap.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef SHARK_TEST_CUBEMAP_PATH
#error SHARK_TEST_CUBEMAP_PATH must identify the S-001 DDS fixture
#endif

namespace {

constexpr std::size_t dds_flags_offset = 8;
constexpr std::size_t dds_height_offset = 12;
constexpr std::size_t dds_width_offset = 16;
constexpr std::size_t dds_mip_count_offset = 28;
constexpr std::size_t dds_pixel_format_flags_offset = 80;
constexpr std::size_t dds_pixel_format_four_cc_offset = 84;
constexpr std::size_t dds_rgb_bit_count_offset = 88;
constexpr std::size_t dds_red_mask_offset = 92;
constexpr std::size_t dds_green_mask_offset = 96;
constexpr std::size_t dds_blue_mask_offset = 100;
constexpr std::size_t dds_alpha_mask_offset = 104;
constexpr std::size_t dds_caps_offset = 108;
constexpr std::size_t dds_caps2_offset = 112;
constexpr std::size_t dds_dx10_format_offset = 128;
constexpr std::size_t dds_dx10_dimension_offset = 132;
constexpr std::size_t dds_dx10_misc_offset = 136;
constexpr std::size_t dds_dx10_array_size_offset = 140;
constexpr std::size_t dds_pixel_data_offset = 148;
constexpr std::uint32_t dxgi_format_rgba8_typeless = 27;
constexpr std::uint32_t dxgi_format_rgba8_unorm = 28;
constexpr std::uint32_t dxgi_format_bgra8_unorm_srgb = 91;

[[nodiscard]] std::filesystem::path fixture_path()
{
    return std::filesystem::path{SHARK_TEST_CUBEMAP_PATH};
}

[[nodiscard]] std::vector<std::byte> read_fixture()
{
    const auto path = fixture_path();
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        throw std::runtime_error(
            "Could not open DDS test fixture: " + path.string());
    }
    const auto end = stream.tellg();
    if (end <= 0) {
        throw std::runtime_error(
            "DDS test fixture is empty: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error(
            "Could not read DDS test fixture: " + path.string());
    }
    return bytes;
}

void write_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    if (offset > bytes.size() ||
        bytes.size() - offset < sizeof(value)) {
        throw std::out_of_range("DDS mutation offset is out of range");
    }
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        const auto shift = static_cast<unsigned int>(index * 8U);
        bytes[offset + index] = static_cast<std::byte>(
            (value >> shift) & 0xFFU);
    }
}

[[nodiscard]] std::array<std::uint8_t, 4> pixel_at(
    const shark::assets::CubemapSubresourceView& subresource,
    const std::uint32_t x,
    const std::uint32_t y)
{
    if (x >= subresource.width ||
        y >= subresource.height) {
        throw std::out_of_range("DDS pixel coordinate is out of range");
    }
    const auto offset =
        static_cast<std::size_t>(y) * subresource.row_pitch +
        static_cast<std::size_t>(x) * 4U;
    if (offset > subresource.pixels.size() ||
        subresource.pixels.size() - offset < 4U) {
        throw std::out_of_range("DDS pixel data is out of range");
    }
    return {
        std::to_integer<std::uint8_t>(subresource.pixels[offset]),
        std::to_integer<std::uint8_t>(subresource.pixels[offset + 1U]),
        std::to_integer<std::uint8_t>(subresource.pixels[offset + 2U]),
        std::to_integer<std::uint8_t>(subresource.pixels[offset + 3U]),
    };
}

[[nodiscard]] std::vector<std::byte> make_legacy_rgba8(
    std::vector<std::byte> bytes)
{
    write_u32(bytes, dds_pixel_format_flags_offset, 0x00000041U);
    write_u32(bytes, dds_pixel_format_four_cc_offset, 0);
    write_u32(bytes, dds_rgb_bit_count_offset, 32);
    write_u32(bytes, dds_red_mask_offset, 0x000000FFU);
    write_u32(bytes, dds_green_mask_offset, 0x0000FF00U);
    write_u32(bytes, dds_blue_mask_offset, 0x00FF0000U);
    write_u32(bytes, dds_alpha_mask_offset, 0xFF000000U);
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(dds_dx10_format_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(dds_pixel_data_offset));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_complete_mip_chain(
    const std::vector<std::byte>& source)
{
    constexpr std::uint32_t base_dimension = 8;
    constexpr std::uint32_t mip_levels = 4;
    constexpr std::size_t base_face_bytes =
        base_dimension * base_dimension * 4U;

    if (source.size() !=
        dds_pixel_data_offset +
            shark::assets::cubemap_face_count * base_face_bytes) {
        throw std::runtime_error(
            "Unexpected DDS fixture size while building mip-chain test");
    }

    std::vector<std::byte> result{
        source.begin(),
        source.begin() +
            static_cast<std::ptrdiff_t>(dds_pixel_data_offset),
    };
    write_u32(result, dds_flags_offset, 0x0002100FU);
    write_u32(result, dds_mip_count_offset, mip_levels);
    write_u32(result, dds_caps_offset, 0x00401008U);

    constexpr std::array<std::uint8_t, 6> face_tags{
        32, 64, 96, 128, 160, 192,
    };
    for (std::size_t face = 0;
         face < shark::assets::cubemap_face_count;
         ++face) {
        const auto face_begin =
            source.begin() +
            static_cast<std::ptrdiff_t>(
                dds_pixel_data_offset + face * base_face_bytes);
        result.insert(
            result.end(),
            face_begin,
            face_begin +
                static_cast<std::ptrdiff_t>(base_face_bytes));

        for (std::uint32_t mip = 1; mip < mip_levels; ++mip) {
            const auto dimension = base_dimension >> mip;
            for (std::uint32_t y = 0; y < dimension; ++y) {
                for (std::uint32_t x = 0; x < dimension; ++x) {
                    result.push_back(
                        static_cast<std::byte>(face_tags[face]));
                    result.push_back(static_cast<std::byte>(
                        16U + x * 24U));
                    result.push_back(static_cast<std::byte>(
                        16U + y * 24U));
                    result.push_back(static_cast<std::byte>(255));
                }
            }
        }
    }
    return result;
}

void require_asset_failure(
    const shark::core::Result<shark::assets::DdsCubemap>& result,
    const shark::core::ErrorCode expected_code)
{
    REQUIRE_FALSE(result);
    REQUIRE(
        result.error().category() ==
        shark::core::ErrorCategory::assets);
    REQUIRE(result.error().code() == expected_code);
    REQUIRE_FALSE(result.error().message().empty());
}

} // namespace

TEST_CASE(
    "DDS cubemap loader owns deterministic face-major orientation data",
    "[assets][dds][cubemap]")
{
    using namespace shark;

    auto file_result = assets::load_dds_cubemap_file(
        fixture_path(),
        assets::TextureColorSpace::srgb);
    REQUIRE(file_result);

    auto cubemap = std::move(file_result).value();
    REQUIRE(cubemap.width() == 8);
    REQUIRE(cubemap.height() == 8);
    REQUIRE(cubemap.mip_levels() == 1);
    REQUIRE(
        cubemap.format() ==
        assets::TextureFormat::rgba8_unorm_srgb);
    REQUIRE(
        cubemap.color_space() ==
        assets::TextureColorSpace::srgb);
    REQUIRE(
        cubemap.subresource_count() ==
        assets::cubemap_face_count);

    constexpr std::array<std::uint8_t, 6> expected_face_tags{
        32, 64, 96, 128, 160, 192,
    };
    for (std::size_t face_index = 0;
         face_index < assets::cubemap_face_count;
         ++face_index) {
        CAPTURE(face_index);
        const auto face =
            static_cast<assets::CubemapFace>(face_index);
        const auto by_index = cubemap.subresource(face_index);
        const auto by_face = cubemap.subresource(face, 0);
        REQUIRE(by_index);
        REQUIRE(by_face);
        REQUIRE(by_index->face == face);
        REQUIRE(by_index->mip_level == 0);
        REQUIRE(by_index->width == 8);
        REQUIRE(by_index->height == 8);
        REQUIRE(by_index->row_pitch >= 32);
        REQUIRE(by_index->slice_pitch >= 256);
        REQUIRE(
            by_index->pixels.data() ==
            by_face->pixels.data());
        REQUIRE((
            pixel_at(*by_index, 0, 0) ==
            std::array<std::uint8_t, 4>{
                expected_face_tags[face_index],
                16,
                16,
                255,
            }));
        REQUIRE((
            pixel_at(*by_index, 7, 0) ==
            std::array<std::uint8_t, 4>{
                expected_face_tags[face_index],
                184,
                16,
                255,
            }));
        REQUIRE((
            pixel_at(*by_index, 0, 7) ==
            std::array<std::uint8_t, 4>{
                expected_face_tags[face_index],
                16,
                184,
                255,
            }));
    }

    REQUIRE_FALSE(cubemap.subresource(
        assets::cubemap_face_count));
    REQUIRE_FALSE(cubemap.subresource(
        assets::CubemapFace::positive_x,
        1));
    REQUIRE_FALSE(cubemap.subresource(
        static_cast<assets::CubemapFace>(255),
        0));
}

TEST_CASE(
    "DDS cubemap loader accepts explicit partial and complete mip chains",
    "[assets][dds][cubemap]")
{
    using namespace shark;

    const auto fixture = read_fixture();
    const auto partial = assets::load_dds_cubemap(
        fixture,
        assets::TextureColorSpace::srgb);
    REQUIRE(partial);
    REQUIRE(partial.value().mip_levels() == 1);

    const auto full_bytes = make_complete_mip_chain(fixture);
    const auto full = assets::load_dds_cubemap(
        full_bytes,
        assets::TextureColorSpace::srgb);
    REQUIRE(full);
    REQUIRE(full.value().mip_levels() == 4);
    REQUIRE(
        full.value().subresource_count() ==
        assets::cubemap_face_count * 4U);

    for (std::size_t face_index = 0;
         face_index < assets::cubemap_face_count;
         ++face_index) {
        for (std::uint32_t mip = 0; mip < 4; ++mip) {
            CAPTURE(face_index, mip);
            const auto index = face_index * 4U + mip;
            const auto subresource =
                full.value().subresource(index);
            REQUIRE(subresource);
            REQUIRE(
                subresource->face ==
                static_cast<assets::CubemapFace>(face_index));
            REQUIRE(subresource->mip_level == mip);
            REQUIRE(
                subresource->width ==
                std::max<std::uint32_t>(1, 8U >> mip));
            REQUIRE(subresource->height == subresource->width);
        }
    }
}

TEST_CASE(
    "DDS cubemap format and color-space semantics are explicit",
    "[assets][dds][cubemap]")
{
    using namespace shark;

    const auto fixture = read_fixture();

    SECTION("the expected color space must match the DDS format")
    {
        const auto result = assets::load_dds_cubemap(
            fixture,
            assets::TextureColorSpace::linear);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("linear RGBA8 remains linear")
    {
        auto bytes = fixture;
        write_u32(
            bytes,
            dds_dx10_format_offset,
            dxgi_format_rgba8_unorm);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::linear);
        REQUIRE(result);
        REQUIRE(
            result.value().format() ==
            assets::TextureFormat::rgba8_unorm);
        REQUIRE(
            result.value().color_space() ==
            assets::TextureColorSpace::linear);
    }

    SECTION("typeless RGBA8 is ambiguous")
    {
        auto bytes = fixture;
        write_u32(
            bytes,
            dds_dx10_format_offset,
            dxgi_format_rgba8_typeless);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::linear);
        require_asset_failure(
            result,
            core::ErrorCode::unsupported);
    }

    SECTION("other four-channel formats are outside S-001")
    {
        auto bytes = fixture;
        write_u32(
            bytes,
            dds_dx10_format_offset,
            dxgi_format_bgra8_unorm_srgb);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::unsupported);
    }

    SECTION("legacy DDS color space is rejected as ambiguous")
    {
        const auto legacy = make_legacy_rgba8(fixture);
        const auto result = assets::load_dds_cubemap(
            legacy,
            assets::TextureColorSpace::linear);
        require_asset_failure(
            result,
            core::ErrorCode::unsupported);
    }

    SECTION("invalid expected color-space values are rejected")
    {
        const auto result = assets::load_dds_cubemap(
            fixture,
            static_cast<assets::TextureColorSpace>(0));
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }
}

TEST_CASE(
    "DDS cubemap loader rejects malformed dimensions faces and mip data",
    "[assets][dds][cubemap]")
{
    using namespace shark;

    const auto fixture = read_fixture();

    SECTION("faces must be square")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_width_offset, 4);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("height must be nonzero")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_height_offset, 0);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().category() ==
            core::ErrorCategory::assets);
    }

    SECTION("a two-dimensional texture must remain a cubemap")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_dx10_misc_offset, 0);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("exactly one complete six-face cube is accepted")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_dx10_array_size_offset, 2);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("legacy incomplete cube faces cannot enter the asset boundary")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_caps2_offset, 0x00007E00U);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("mip count cannot exceed the mathematical chain")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_mip_count_offset, 5);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("declared partial mips must have complete pixel data")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_flags_offset, 0x0002100FU);
        write_u32(bytes, dds_mip_count_offset, 2);
        write_u32(bytes, dds_caps_offset, 0x00401008U);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().category() ==
            core::ErrorCategory::assets);
    }

    SECTION("truncated last-face data is rejected")
    {
        auto bytes = fixture;
        bytes.resize(bytes.size() - 1U);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("trailing bytes are rejected")
    {
        auto bytes = fixture;
        bytes.push_back(static_cast<std::byte>(0));
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        require_asset_failure(
            result,
            core::ErrorCode::invalid_argument);
    }

    SECTION("three-dimensional metadata is rejected")
    {
        auto bytes = fixture;
        write_u32(bytes, dds_dx10_dimension_offset, 4);
        const auto result = assets::load_dds_cubemap(
            bytes,
            assets::TextureColorSpace::srgb);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().category() ==
            core::ErrorCategory::assets);
    }
}

TEST_CASE(
    "DDS cubemap file failures are structured asset errors",
    "[assets][dds][cubemap]")
{
    using namespace shark;

    const auto missing =
        fixture_path().parent_path() /
        "missing_shark_cubemap.dds";
    const auto result = assets::load_dds_cubemap_file(
        missing,
        assets::TextureColorSpace::srgb);
    require_asset_failure(
        result,
        core::ErrorCode::not_found);
}
