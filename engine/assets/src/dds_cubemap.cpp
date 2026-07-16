#include <shark/assets/dds_cubemap.hpp>

#include <shark/core/error.hpp>

#include <DirectXTex.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace shark::assets {
namespace {

constexpr std::size_t maximum_encoded_dds_bytes =
    256U * 1024U * 1024U;
constexpr std::size_t rgba8_bytes_per_pixel = 4;
constexpr std::size_t dx10_dds_header_bytes = 148;
constexpr std::size_t dds_caps2_offset = 112;
constexpr std::uint32_t dds_caps2_all_cubemap_faces = 0x0000FE00U;

[[nodiscard]] core::Error asset_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::assets,
        code,
        std::move(message),
    };
}

[[nodiscard]] std::string hresult_text(const HRESULT result)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return stream.str();
}

[[nodiscard]] bool valid_expected_color_space(
    const TextureColorSpace color_space) noexcept
{
    return color_space == TextureColorSpace::linear ||
        color_space == TextureColorSpace::srgb;
}

[[nodiscard]] std::size_t maximum_mip_levels(
    std::size_t dimension) noexcept
{
    std::size_t levels = 1;
    while (dimension > 1) {
        dimension >>= 1U;
        ++levels;
    }
    return levels;
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        const auto shift = static_cast<unsigned int>(index * 8U);
        value |=
            std::to_integer<std::uint32_t>(bytes[offset + index])
            << shift;
    }
    return value;
}

[[nodiscard]] core::Result<TextureFormat> validated_format(
    const DXGI_FORMAT format,
    const TextureColorSpace expected_color_space)
{
    TextureFormat shark_format{};
    TextureColorSpace actual_color_space{};

    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        shark_format = TextureFormat::rgba8_unorm;
        actual_color_space = TextureColorSpace::linear;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        shark_format = TextureFormat::rgba8_unorm_srgb;
        actual_color_space = TextureColorSpace::srgb;
        break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return core::Result<TextureFormat>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap uses a typeless RGBA8 format with ambiguous "
            "color-space semantics"));
    default:
        return core::Result<TextureFormat>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap format is unsupported; Shark S-001 accepts only "
            "RGBA8 UNORM and RGBA8 UNORM SRGB"));
    }

    if (actual_color_space != expected_color_space) {
        return core::Result<TextureFormat>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap color space does not match the caller's explicit "
            "expectation"));
    }
    return core::Result<TextureFormat>::success(shark_format);
}

[[nodiscard]] core::Result<void> validate_metadata(
    const DirectX::TexMetadata& metadata,
    const DirectX::DDSMetaData& dds_metadata,
    const std::span<const std::byte> encoded_dds)
{
    if (!dds_metadata.IsDX10()) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap requires a DX10 extension header so its format and "
            "color space are explicit"));
    }
    if (encoded_dds.size() < dx10_dds_header_bytes) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap is missing its complete DX10 header"));
    }
    const auto caps2 = read_u32(encoded_dds, dds_caps2_offset);
    if ((caps2 & dds_caps2_all_cubemap_faces) !=
        dds_caps2_all_cubemap_faces) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap header does not declare all six legacy face caps"));
    }
    if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
        metadata.depth != 1) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap must be a two-dimensional texture"));
    }
    if (!metadata.IsCubemap()) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS texture is not marked as a cubemap"));
    }
    if (metadata.arraySize != cubemap_face_count) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap must contain exactly one complete set of six faces"));
    }
    if (metadata.width == 0 ||
        metadata.height == 0 ||
        metadata.width != metadata.height) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap faces must have equal, nonzero width and height"));
    }
    if (metadata.width >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()) ||
        metadata.height >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap dimensions exceed Shark's 32-bit texture extent"));
    }

    const auto maximum_levels = maximum_mip_levels(metadata.width);
    if (metadata.mipLevels == 0 ||
        metadata.mipLevels > maximum_levels ||
        metadata.mipLevels >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap mip chain is malformed for its face dimensions"));
    }

    std::size_t face_payload_bytes = 0;
    for (std::size_t mip = 0;
         mip < metadata.mipLevels;
         ++mip) {
        const auto mip_width =
            std::max<std::size_t>(1, metadata.width >> mip);
        const auto mip_height =
            std::max<std::size_t>(1, metadata.height >> mip);
        if (mip_width >
                std::numeric_limits<std::size_t>::max() /
                    rgba8_bytes_per_pixel ||
            mip_height >
                std::numeric_limits<std::size_t>::max() /
                    (mip_width * rgba8_bytes_per_pixel)) {
            return core::Result<void>::failure(asset_error(
                core::ErrorCode::unsupported,
                "DDS cubemap mip payload exceeds addressable memory"));
        }
        const auto mip_bytes =
            mip_width * rgba8_bytes_per_pixel * mip_height;
        if (face_payload_bytes >
            std::numeric_limits<std::size_t>::max() - mip_bytes) {
            return core::Result<void>::failure(asset_error(
                core::ErrorCode::unsupported,
                "DDS cubemap mip payload exceeds addressable memory"));
        }
        face_payload_bytes += mip_bytes;
    }
    if (face_payload_bytes >
        (std::numeric_limits<std::size_t>::max() -
            dx10_dds_header_bytes) /
            cubemap_face_count) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap face payload exceeds addressable memory"));
    }
    const auto exact_encoded_bytes =
        dx10_dds_header_bytes +
        face_payload_bytes * cubemap_face_count;
    if (encoded_dds.size() != exact_encoded_bytes) {
        return core::Result<void>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap payload size does not exactly match its six-face "
            "mip chain"));
    }

    return core::Result<void>::success();
}

[[nodiscard]] core::Result<std::vector<std::byte>> read_file(
    const std::filesystem::path& path)
{
    if (path.empty()) {
        return core::Result<std::vector<std::byte>>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap file path must not be empty"));
    }

    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        return core::Result<std::vector<std::byte>>::failure(asset_error(
            core::ErrorCode::not_found,
            "DDS cubemap file could not be opened: " + path.string()));
    }

    const auto end = stream.tellg();
    if (end <= 0) {
        return core::Result<std::vector<std::byte>>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap file is empty or its size could not be read: " +
                path.string()));
    }
    const auto file_size = static_cast<std::uintmax_t>(end);
    if (file_size > maximum_encoded_dds_bytes) {
        return core::Result<std::vector<std::byte>>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap file exceeds Shark's bounded startup asset size"));
    }

    std::vector<std::byte> encoded(
        static_cast<std::size_t>(file_size));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size()));
    if (!stream) {
        return core::Result<std::vector<std::byte>>::failure(asset_error(
            core::ErrorCode::operation_failed,
            "DDS cubemap file could not be read completely: " +
                path.string()));
    }
    return core::Result<std::vector<std::byte>>::success(
        std::move(encoded));
}

} // namespace

DdsCubemap::DdsCubemap(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t mip_levels,
    const TextureFormat format,
    const TextureColorSpace color_space,
    std::vector<SubresourceStorage> subresources) noexcept
    : width_(width)
    , height_(height)
    , mip_levels_(mip_levels)
    , format_(format)
    , color_space_(color_space)
    , subresources_(std::move(subresources))
{
}

std::uint32_t DdsCubemap::width() const noexcept
{
    return width_;
}

std::uint32_t DdsCubemap::height() const noexcept
{
    return height_;
}

std::uint32_t DdsCubemap::mip_levels() const noexcept
{
    return mip_levels_;
}

TextureFormat DdsCubemap::format() const noexcept
{
    return format_;
}

TextureColorSpace DdsCubemap::color_space() const noexcept
{
    return color_space_;
}

std::size_t DdsCubemap::subresource_count() const noexcept
{
    return subresources_.size();
}

std::optional<CubemapSubresourceView> DdsCubemap::subresource(
    const std::size_t index) const noexcept
{
    if (index >= subresources_.size()) {
        return std::nullopt;
    }
    const auto& stored = subresources_[index];
    return CubemapSubresourceView{
        .face = stored.face,
        .mip_level = stored.mip_level,
        .width = stored.width,
        .height = stored.height,
        .row_pitch = stored.row_pitch,
        .slice_pitch = stored.slice_pitch,
        .pixels = stored.pixels,
    };
}

std::optional<CubemapSubresourceView> DdsCubemap::subresource(
    const CubemapFace face,
    const std::uint32_t mip_level) const noexcept
{
    const auto face_index = static_cast<std::size_t>(face);
    if (face_index >= cubemap_face_count ||
        mip_level >= mip_levels_) {
        return std::nullopt;
    }
    const auto index =
        face_index * static_cast<std::size_t>(mip_levels_) +
        static_cast<std::size_t>(mip_level);
    return subresource(index);
}

core::Result<DdsCubemap> load_dds_cubemap(
    const std::span<const std::byte> encoded_dds,
    const TextureColorSpace expected_color_space)
{
    if (!valid_expected_color_space(expected_color_space)) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap expected color space is invalid"));
    }
    if (encoded_dds.empty()) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap memory is empty"));
    }
    if (encoded_dds.size() > maximum_encoded_dds_bytes) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap memory exceeds Shark's bounded startup asset size"));
    }

    DirectX::TexMetadata metadata{};
    DirectX::DDSMetaData dds_metadata{};
    const auto metadata_result = DirectX::GetMetadataFromDDSMemoryEx(
        encoded_dds.data(),
        encoded_dds.size(),
        DirectX::DDS_FLAGS_NONE,
        metadata,
        &dds_metadata);
    if (FAILED(metadata_result)) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap metadata could not be decoded (HRESULT " +
                hresult_text(metadata_result) + ")"));
    }

    if (!dds_metadata.IsDX10()) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::unsupported,
            "DDS cubemap requires a DX10 extension header so its format and "
            "color space are explicit"));
    }
    auto format_result =
        validated_format(metadata.format, expected_color_space);
    if (!format_result) {
        return core::Result<DdsCubemap>::failure(
            std::move(format_result).error());
    }
    const auto shark_format = format_result.value();

    auto metadata_validation = validate_metadata(
        metadata,
        dds_metadata,
        encoded_dds);
    if (!metadata_validation) {
        return core::Result<DdsCubemap>::failure(
            std::move(metadata_validation).error());
    }

    DirectX::ScratchImage decoded;
    DirectX::TexMetadata decoded_metadata{};
    DirectX::DDSMetaData decoded_dds_metadata{};
    const auto load_result = DirectX::LoadFromDDSMemoryEx(
        encoded_dds.data(),
        encoded_dds.size(),
        DirectX::DDS_FLAGS_NONE,
        &decoded_metadata,
        &decoded_dds_metadata,
        decoded);
    if (FAILED(load_result)) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::invalid_argument,
            "DDS cubemap pixel data or mip chain could not be decoded "
            "(HRESULT " +
                hresult_text(load_result) + ")"));
    }

    const auto expected_image_count =
        cubemap_face_count * metadata.mipLevels;
    if (decoded.GetImageCount() != expected_image_count ||
        decoded_metadata.width != metadata.width ||
        decoded_metadata.height != metadata.height ||
        decoded_metadata.depth != metadata.depth ||
        decoded_metadata.arraySize != metadata.arraySize ||
        decoded_metadata.mipLevels != metadata.mipLevels ||
        decoded_metadata.format != metadata.format ||
        decoded_metadata.dimension != metadata.dimension ||
        decoded_metadata.miscFlags != metadata.miscFlags ||
        decoded_metadata.miscFlags2 != metadata.miscFlags2 ||
        !decoded_dds_metadata.IsDX10()) {
        return core::Result<DdsCubemap>::failure(asset_error(
            core::ErrorCode::invalid_state,
            "DDS cubemap decoded metadata does not match its preflight "
            "metadata"));
    }

    std::vector<DdsCubemap::SubresourceStorage> subresources;
    subresources.reserve(expected_image_count);
    for (std::size_t face = 0;
         face < cubemap_face_count;
         ++face) {
        for (std::size_t mip = 0;
             mip < metadata.mipLevels;
             ++mip) {
            const auto* image = decoded.GetImage(mip, face, 0);
            const auto expected_width =
                std::max<std::size_t>(1, metadata.width >> mip);
            const auto expected_height =
                std::max<std::size_t>(1, metadata.height >> mip);
            if (image == nullptr ||
                image->pixels == nullptr ||
                image->format != metadata.format ||
                image->width != expected_width ||
                image->height != expected_height) {
                return core::Result<DdsCubemap>::failure(asset_error(
                    core::ErrorCode::invalid_argument,
                    "DDS cubemap contains an incomplete or malformed face "
                    "mip"));
            }

            const auto minimum_row_pitch =
                expected_width * rgba8_bytes_per_pixel;
            if (image->rowPitch != minimum_row_pitch ||
                image->height >
                    std::numeric_limits<std::size_t>::max() /
                        image->rowPitch ||
                image->slicePitch !=
                    image->rowPitch * image->height) {
                return core::Result<DdsCubemap>::failure(asset_error(
                    core::ErrorCode::invalid_argument,
                    "DDS cubemap contains invalid subresource pitches"));
            }

            std::vector<std::byte> pixels(image->slicePitch);
            std::copy_n(
                reinterpret_cast<const std::byte*>(image->pixels),
                image->slicePitch,
                pixels.begin());
            subresources.push_back(DdsCubemap::SubresourceStorage{
                .face = static_cast<CubemapFace>(face),
                .mip_level = static_cast<std::uint32_t>(mip),
                .width = static_cast<std::uint32_t>(image->width),
                .height = static_cast<std::uint32_t>(image->height),
                .row_pitch = image->rowPitch,
                .slice_pitch = image->slicePitch,
                .pixels = std::move(pixels),
            });
        }
    }

    return core::Result<DdsCubemap>::success(DdsCubemap{
        static_cast<std::uint32_t>(metadata.width),
        static_cast<std::uint32_t>(metadata.height),
        static_cast<std::uint32_t>(metadata.mipLevels),
        shark_format,
        expected_color_space,
        std::move(subresources),
    });
}

core::Result<DdsCubemap> load_dds_cubemap_file(
    const std::filesystem::path& path,
    const TextureColorSpace expected_color_space)
{
    auto encoded = read_file(path);
    if (!encoded) {
        return core::Result<DdsCubemap>::failure(
            std::move(encoded).error());
    }
    return load_dds_cubemap(
        encoded.value(),
        expected_color_space);
}

} // namespace shark::assets
