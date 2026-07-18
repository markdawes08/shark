#include <shark/assets/environment_lighting.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace shark::assets {
namespace {

constexpr double pi = 3.1415926535897932384626433832795;
constexpr std::uint32_t specular_sample_count = 128;
constexpr std::uint32_t brdf_sample_count = 128;

struct Vector3 final {
    double x{};
    double y{};
    double z{};
};

struct Pixel final {
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

static_assert(sizeof(Pixel) == environment_texel_bytes);

struct Image final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<Pixel> pixels;
};

struct CubeImage final {
    CubemapFace face{CubemapFace::positive_x};
    std::uint32_t mip_level{};
    Image image;
};

struct SourceSample final {
    Vector3 direction;
    Pixel radiance;
    double solid_angle{};
};

[[nodiscard]] core::Error environment_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::assets,
        core::ErrorCode::invalid_state,
        std::move(message),
    };
}

[[nodiscard]] constexpr Vector3 add(
    const Vector3 left,
    const Vector3 right) noexcept
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] constexpr Vector3 subtract(
    const Vector3 left,
    const Vector3 right) noexcept
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

[[nodiscard]] constexpr Vector3 scale(
    const Vector3 value,
    const double scalar) noexcept
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

[[nodiscard]] constexpr double dot(
    const Vector3 left,
    const Vector3 right) noexcept
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

[[nodiscard]] constexpr Vector3 cross(
    const Vector3 left,
    const Vector3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vector3 normalized(
    const Vector3 value) noexcept
{
    const auto length_squared = dot(value, value);
    if (!std::isfinite(length_squared) ||
        length_squared <= std::numeric_limits<double>::min()) {
        return {};
    }
    return scale(value, 1.0 / std::sqrt(length_squared));
}

[[nodiscard]] constexpr Pixel add(
    const Pixel left,
    const Pixel right) noexcept
{
    return {
        left.red + right.red,
        left.green + right.green,
        left.blue + right.blue,
        left.alpha + right.alpha,
    };
}

[[nodiscard]] constexpr Pixel lerp(
    const Pixel from,
    const Pixel to,
    const double amount) noexcept
{
    const auto inverse = 1.0 - amount;
    return {
        static_cast<float>(
            static_cast<double>(from.red) * inverse +
            static_cast<double>(to.red) * amount),
        static_cast<float>(
            static_cast<double>(from.green) * inverse +
            static_cast<double>(to.green) * amount),
        static_cast<float>(
            static_cast<double>(from.blue) * inverse +
            static_cast<double>(to.blue) * amount),
        1.0F,
    };
}

[[nodiscard]] bool finite_nonnegative(
    const Pixel value) noexcept
{
    return std::isfinite(value.red) &&
        std::isfinite(value.green) &&
        std::isfinite(value.blue) &&
        std::isfinite(value.alpha) &&
        value.red >= 0.0F &&
        value.green >= 0.0F &&
        value.blue >= 0.0F &&
        value.alpha >= 0.0F;
}

[[nodiscard]] constexpr float peak_rgb(
    const Pixel value) noexcept
{
    return std::max({
        value.red,
        value.green,
        value.blue,
    });
}

[[nodiscard]] Vector3 latitude_longitude_direction(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t width,
    const std::uint32_t height) noexcept
{
    const auto u =
        (static_cast<double>(x) + 0.5) /
        static_cast<double>(width);
    const auto v =
        (static_cast<double>(y) + 0.5) /
        static_cast<double>(height);
    const auto longitude = (u - 0.5) * (2.0 * pi);
    const auto latitude = (0.5 - v) * pi;
    const auto latitude_cosine = std::cos(latitude);
    return normalized({
        latitude_cosine * std::sin(longitude),
        std::sin(latitude),
        -latitude_cosine * std::cos(longitude),
    });
}

[[nodiscard]] Vector3 cubemap_direction(
    const CubemapFace face,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t dimension) noexcept
{
    const auto horizontal =
        2.0 *
            (static_cast<double>(x) + 0.5) /
            static_cast<double>(dimension) -
        1.0;
    const auto vertical =
        2.0 *
            (static_cast<double>(y) + 0.5) /
            static_cast<double>(dimension) -
        1.0;

    Vector3 direction{};
    switch (face) {
    case CubemapFace::positive_x:
        direction = {1.0, -vertical, -horizontal};
        break;
    case CubemapFace::negative_x:
        direction = {-1.0, -vertical, horizontal};
        break;
    case CubemapFace::positive_y:
        direction = {horizontal, 1.0, vertical};
        break;
    case CubemapFace::negative_y:
        direction = {horizontal, -1.0, -vertical};
        break;
    case CubemapFace::positive_z:
        direction = {horizontal, -vertical, 1.0};
        break;
    case CubemapFace::negative_z:
        direction = {-horizontal, -vertical, -1.0};
        break;
    }
    return normalized(direction);
}

[[nodiscard]] Pixel evaluate_hdr_daylight(
    const Vector3 direction) noexcept
{
    constexpr Pixel zenith{
        0.08F,
        0.30F,
        0.92F,
        1.0F,
    };
    constexpr Pixel horizon{
        0.62F,
        0.90F,
        1.18F,
        1.0F,
    };
    constexpr Pixel nadir{
        0.07F,
        0.09F,
        0.12F,
        1.0F,
    };
    const auto vertical = std::clamp(direction.y, -1.0, 1.0);
    const auto gradient = vertical >= 0.0
        ? std::pow(vertical, 0.65)
        : std::pow(-vertical, 0.75);
    // The directional sun is deliberately not baked into this low-resolution
    // source. The renderer evaluates its smooth disk and direct light from the
    // shared DaylightSettings, avoiding cubemap texel blocks and duplicate
    // energy in the environment convolution.
    return vertical >= 0.0
        ? lerp(horizon, zenith, gradient)
        : lerp(horizon, nadir, gradient);
}

[[nodiscard]] Image generate_source()
{
    Image source{
        .width = environment_source_width,
        .height = environment_source_height,
        .pixels = {},
    };
    source.pixels.reserve(environment_source_texel_count);
    for (std::uint32_t y = 0; y < source.height; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
            source.pixels.push_back(evaluate_hdr_daylight(
                latitude_longitude_direction(
                    x,
                    y,
                    source.width,
                    source.height)));
        }
    }
    return source;
}

[[nodiscard]] std::uint32_t wrapped_x(
    const std::int64_t value,
    const std::uint32_t width) noexcept
{
    const auto signed_width = static_cast<std::int64_t>(width);
    auto wrapped = value % signed_width;
    if (wrapped < 0) {
        wrapped += signed_width;
    }
    return static_cast<std::uint32_t>(wrapped);
}

[[nodiscard]] Pixel sample_source(
    const Image& source,
    const Vector3 direction) noexcept
{
    const auto unit = normalized(direction);
    auto u =
        std::atan2(unit.x, -unit.z) / (2.0 * pi) +
        0.5;
    u -= std::floor(u);
    const auto v =
        0.5 -
        std::asin(std::clamp(unit.y, -1.0, 1.0)) / pi;
    const auto source_x =
        u * static_cast<double>(source.width) - 0.5;
    const auto source_y =
        std::clamp(
            v * static_cast<double>(source.height) - 0.5,
            0.0,
            static_cast<double>(source.height - 1U));
    const auto floor_x = static_cast<std::int64_t>(
        std::floor(source_x));
    const auto floor_y = static_cast<std::uint32_t>(
        std::floor(source_y));
    const auto next_y = std::min(
        floor_y + 1U,
        source.height - 1U);
    const auto amount_x = source_x -
        static_cast<double>(floor_x);
    const auto amount_y = source_y -
        static_cast<double>(floor_y);
    const auto x0 = wrapped_x(floor_x, source.width);
    const auto x1 = wrapped_x(floor_x + 1, source.width);
    const auto row0 =
        static_cast<std::size_t>(floor_y) * source.width;
    const auto row1 =
        static_cast<std::size_t>(next_y) * source.width;
    const auto top = lerp(
        source.pixels[row0 + x0],
        source.pixels[row0 + x1],
        amount_x);
    const auto bottom = lerp(
        source.pixels[row1 + x0],
        source.pixels[row1 + x1],
        amount_x);
    return lerp(top, bottom, amount_y);
}

[[nodiscard]] Image downsample_2x2(const Image& source)
{
    const auto width = source.width / 2U;
    const auto height = source.height / 2U;
    Image result{
        .width = width,
        .height = height,
        .pixels = {},
    };
    result.pixels.resize(
        static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto source_x = x * 2U;
            const auto source_y = y * 2U;
            const auto row0 =
                static_cast<std::size_t>(source_y) *
                source.width;
            const auto row1 =
                static_cast<std::size_t>(source_y + 1U) *
                source.width;
            const auto sum = add(
                add(
                    source.pixels[row0 + source_x],
                    source.pixels[row0 + source_x + 1U]),
                add(
                    source.pixels[row1 + source_x],
                    source.pixels[row1 + source_x + 1U]));
            result.pixels[
                static_cast<std::size_t>(y) * width + x] = {
                sum.red * 0.25F,
                sum.green * 0.25F,
                sum.blue * 0.25F,
                1.0F,
            };
        }
    }
    return result;
}

[[nodiscard]] std::vector<CubeImage> build_radiance_cube(
    const Image& source)
{
    std::vector<CubeImage> result;
    result.reserve(environment_radiance_subresource_count);
    for (std::size_t face_index = 0;
         face_index < cubemap_face_count;
         ++face_index) {
        const auto face =
            static_cast<CubemapFace>(face_index);
        Image current{
            .width = environment_radiance_dimension,
            .height = environment_radiance_dimension,
            .pixels = {},
        };
        current.pixels.resize(
            static_cast<std::size_t>(current.width) *
            current.height);
        for (std::uint32_t y = 0; y < current.height; ++y) {
            for (std::uint32_t x = 0; x < current.width; ++x) {
                current.pixels[
                    static_cast<std::size_t>(y) *
                        current.width +
                    x] = sample_source(
                    source,
                    cubemap_direction(
                        face,
                        x,
                        y,
                        current.width));
            }
        }
        result.push_back({
            .face = face,
            .mip_level = 0,
            .image = current,
        });
        for (std::uint32_t mip = 1;
             mip < environment_radiance_mip_levels;
             ++mip) {
            current = downsample_2x2(current);
            result.push_back({
                .face = face,
                .mip_level = mip,
                .image = current,
            });
        }
    }
    return result;
}

[[nodiscard]] std::vector<SourceSample> build_source_samples(
    const Image& source)
{
    std::vector<SourceSample> samples;
    samples.reserve(source.pixels.size());
    const auto longitude_step =
        2.0 * pi / static_cast<double>(source.width);
    const auto latitude_step =
        pi / static_cast<double>(source.height);
    for (std::uint32_t y = 0; y < source.height; ++y) {
        const auto latitude_top =
            pi * 0.5 -
            static_cast<double>(y) * latitude_step;
        const auto latitude_bottom =
            latitude_top - latitude_step;
        const auto solid_angle =
            longitude_step *
            (std::sin(latitude_top) -
                std::sin(latitude_bottom));
        for (std::uint32_t x = 0; x < source.width; ++x) {
            const auto index =
                static_cast<std::size_t>(y) *
                    source.width +
                x;
            samples.push_back({
                .direction = latitude_longitude_direction(
                    x,
                    y,
                    source.width,
                    source.height),
                .radiance = source.pixels[index],
                .solid_angle = solid_angle,
            });
        }
    }
    return samples;
}

[[nodiscard]] std::vector<CubeImage> build_irradiance_cube(
    const std::vector<SourceSample>& source_samples)
{
    std::vector<CubeImage> result;
    result.reserve(environment_irradiance_subresource_count);
    for (std::size_t face_index = 0;
         face_index < cubemap_face_count;
         ++face_index) {
        const auto face =
            static_cast<CubemapFace>(face_index);
        Image image{
            .width = environment_irradiance_dimension,
            .height = environment_irradiance_dimension,
            .pixels = {},
        };
        image.pixels.resize(
            static_cast<std::size_t>(image.width) *
            image.height);
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const auto normal = cubemap_direction(
                    face,
                    x,
                    y,
                    image.width);
                double red = 0.0;
                double green = 0.0;
                double blue = 0.0;
                for (const auto& sample : source_samples) {
                    const auto cosine = std::max(
                        dot(normal, sample.direction),
                        0.0);
                    const auto weight =
                        cosine * sample.solid_angle;
                    red += static_cast<double>(
                        sample.radiance.red) * weight;
                    green += static_cast<double>(
                        sample.radiance.green) * weight;
                    blue += static_cast<double>(
                        sample.radiance.blue) * weight;
                }
                image.pixels[
                    static_cast<std::size_t>(y) *
                        image.width +
                    x] = {
                    static_cast<float>(red),
                    static_cast<float>(green),
                    static_cast<float>(blue),
                    1.0F,
                };
            }
        }
        result.push_back({
            .face = face,
            .mip_level = 0,
            .image = std::move(image),
        });
    }
    return result;
}

[[nodiscard]] constexpr double radical_inverse(
    std::uint32_t bits) noexcept
{
    bits = (bits << 16U) | (bits >> 16U);
    bits =
        ((bits & 0x55555555U) << 1U) |
        ((bits & 0xAAAAAAAAU) >> 1U);
    bits =
        ((bits & 0x33333333U) << 2U) |
        ((bits & 0xCCCCCCCCU) >> 2U);
    bits =
        ((bits & 0x0F0F0F0FU) << 4U) |
        ((bits & 0xF0F0F0F0U) >> 4U);
    bits =
        ((bits & 0x00FF00FFU) << 8U) |
        ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<double>(bits) *
        2.3283064365386962890625e-10;
}

[[nodiscard]] Vector3 importance_sample_ggx(
    const std::uint32_t sample_index,
    const std::uint32_t sample_count,
    const double roughness,
    const Vector3 normal) noexcept
{
    const auto xi_x =
        static_cast<double>(sample_index) /
        static_cast<double>(sample_count);
    const auto xi_y = radical_inverse(sample_index);
    const auto alpha = roughness * roughness;
    const auto alpha_squared = alpha * alpha;
    const auto phi = 2.0 * pi * xi_x;
    const auto cosine_theta = std::sqrt(
        (1.0 - xi_y) /
        (1.0 + (alpha_squared - 1.0) * xi_y));
    const auto sine_theta = std::sqrt(std::max(
        0.0,
        1.0 - cosine_theta * cosine_theta));
    const Vector3 local_half{
        std::cos(phi) * sine_theta,
        std::sin(phi) * sine_theta,
        cosine_theta,
    };

    const auto helper = std::abs(normal.z) < 0.999
        ? Vector3{0.0, 0.0, 1.0}
        : Vector3{1.0, 0.0, 0.0};
    const auto tangent = normalized(cross(helper, normal));
    const auto bitangent = cross(normal, tangent);
    return normalized(add(
        add(
            scale(tangent, local_half.x),
            scale(bitangent, local_half.y)),
        scale(normal, local_half.z)));
}

[[nodiscard]] std::vector<CubeImage> build_specular_cube(
    const Image& source,
    const std::vector<CubeImage>& radiance)
{
    std::vector<CubeImage> result;
    result.reserve(environment_specular_subresource_count);
    for (std::size_t face_index = 0;
         face_index < cubemap_face_count;
         ++face_index) {
        const auto face =
            static_cast<CubemapFace>(face_index);
        const auto radiance_base =
            face_index * environment_radiance_mip_levels;
        result.push_back({
            .face = face,
            .mip_level = 0,
            .image = radiance[radiance_base].image,
        });

        for (std::uint32_t mip = 1;
             mip < environment_specular_mip_levels;
             ++mip) {
            const auto dimension = environment_mip_dimension(
                environment_specular_dimension,
                mip);
            const auto roughness =
                static_cast<double>(mip) /
                static_cast<double>(
                    environment_specular_mip_levels - 1U);
            Image image{
                .width = dimension,
                .height = dimension,
                .pixels = {},
            };
            image.pixels.resize(
                static_cast<std::size_t>(dimension) *
                dimension);
            for (std::uint32_t y = 0; y < dimension; ++y) {
                for (std::uint32_t x = 0; x < dimension; ++x) {
                    const auto normal = cubemap_direction(
                        face,
                        x,
                        y,
                        dimension);
                    const auto view = normal;
                    double red = 0.0;
                    double green = 0.0;
                    double blue = 0.0;
                    double total_weight = 0.0;
                    for (std::uint32_t sample_index = 0;
                         sample_index < specular_sample_count;
                         ++sample_index) {
                        const auto half_vector =
                            importance_sample_ggx(
                                sample_index,
                                specular_sample_count,
                                roughness,
                                normal);
                        const auto light = normalized(subtract(
                            scale(
                                half_vector,
                                2.0 * dot(view, half_vector)),
                            view));
                        const auto weight =
                            std::max(dot(normal, light), 0.0);
                        if (weight <= 0.0) {
                            continue;
                        }
                        const auto sample =
                            sample_source(source, light);
                        red += static_cast<double>(
                            sample.red) * weight;
                        green += static_cast<double>(
                            sample.green) * weight;
                        blue += static_cast<double>(
                            sample.blue) * weight;
                        total_weight += weight;
                    }
                    Pixel filtered{};
                    if (total_weight > 0.0) {
                        filtered = {
                            static_cast<float>(
                                red / total_weight),
                            static_cast<float>(
                                green / total_weight),
                            static_cast<float>(
                                blue / total_weight),
                            1.0F,
                        };
                    }
                    else {
                        filtered = sample_source(source, normal);
                    }
                    image.pixels[
                        static_cast<std::size_t>(y) *
                            dimension +
                        x] = filtered;
                }
            }
            result.push_back({
                .face = face,
                .mip_level = mip,
                .image = std::move(image),
            });
        }
    }
    return result;
}

[[nodiscard]] constexpr double geometry_schlick_ggx(
    const double normal_cosine,
    const double roughness) noexcept
{
    const auto alpha = roughness * roughness;
    const auto k = alpha * 0.5;
    return normal_cosine /
        (normal_cosine * (1.0 - k) + k);
}

[[nodiscard]] Image build_brdf_lut()
{
    Image result{
        .width = environment_brdf_lut_dimension,
        .height = environment_brdf_lut_dimension,
        .pixels = {},
    };
    result.pixels.resize(
        environment_brdf_lut_texel_count);
    const Vector3 normal{0.0, 0.0, 1.0};
    for (std::uint32_t y = 0; y < result.height; ++y) {
        const auto roughness =
            (static_cast<double>(y) + 0.5) /
            static_cast<double>(result.height);
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const auto normal_view_cosine =
                (static_cast<double>(x) + 0.5) /
                static_cast<double>(result.width);
            const Vector3 view{
                std::sqrt(std::max(
                    0.0,
                    1.0 -
                        normal_view_cosine *
                            normal_view_cosine)),
                0.0,
                normal_view_cosine,
            };
            double scale_sum = 0.0;
            double bias_sum = 0.0;
            for (std::uint32_t sample_index = 0;
                 sample_index < brdf_sample_count;
                 ++sample_index) {
                const auto half_vector =
                    importance_sample_ggx(
                        sample_index,
                        brdf_sample_count,
                        roughness,
                        normal);
                const auto light = normalized(subtract(
                    scale(
                        half_vector,
                        2.0 * dot(view, half_vector)),
                    view));
                const auto normal_light_cosine =
                    std::max(light.z, 0.0);
                const auto normal_half_cosine =
                    std::max(half_vector.z, 0.0);
                const auto view_half_cosine =
                    std::max(dot(view, half_vector), 0.0);
                if (normal_light_cosine <= 0.0 ||
                    normal_half_cosine <= 0.0 ||
                    view_half_cosine <= 0.0) {
                    continue;
                }
                const auto geometry =
                    geometry_schlick_ggx(
                        normal_view_cosine,
                        roughness) *
                    geometry_schlick_ggx(
                        normal_light_cosine,
                        roughness);
                const auto geometry_visibility =
                    geometry * view_half_cosine /
                    (normal_half_cosine *
                        normal_view_cosine);
                const auto fresnel =
                    std::pow(1.0 - view_half_cosine, 5.0);
                scale_sum +=
                    (1.0 - fresnel) *
                    geometry_visibility;
                bias_sum += fresnel * geometry_visibility;
            }
            result.pixels[
                static_cast<std::size_t>(y) *
                    result.width +
                x] = {
                static_cast<float>(
                    scale_sum /
                    static_cast<double>(brdf_sample_count)),
                static_cast<float>(
                    bias_sum /
                    static_cast<double>(brdf_sample_count)),
                0.0F,
                1.0F,
            };
        }
    }
    return result;
}

[[nodiscard]] bool valid_image(const Image& image) noexcept
{
    if (image.width == 0 ||
        image.height == 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) *
                image.height) {
        return false;
    }
    return std::all_of(
        image.pixels.begin(),
        image.pixels.end(),
        [](const Pixel pixel) {
            return finite_nonnegative(pixel) &&
                pixel.alpha == 1.0F;
        });
}

[[nodiscard]] bool valid_cube_images(
    const std::vector<CubeImage>& images,
    const std::uint32_t base_dimension,
    const std::uint32_t mip_levels) noexcept
{
    if (images.size() !=
        cubemap_face_count * mip_levels) {
        return false;
    }
    for (std::size_t face = 0;
         face < cubemap_face_count;
         ++face) {
        for (std::uint32_t mip = 0;
             mip < mip_levels;
             ++mip) {
            const auto index =
                face * mip_levels + mip;
            const auto dimension = environment_mip_dimension(
                base_dimension,
                mip);
            const auto& record = images[index];
            if (record.face !=
                    static_cast<CubemapFace>(face) ||
                record.mip_level != mip ||
                record.image.width != dimension ||
                record.image.height != dimension ||
                !valid_image(record.image)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] float image_peak(const Image& image) noexcept
{
    float result = 0.0F;
    for (const auto pixel : image.pixels) {
        result = std::max(result, peak_rgb(pixel));
    }
    return result;
}

[[nodiscard]] float cube_peak(
    const std::vector<CubeImage>& images) noexcept
{
    float result = 0.0F;
    for (const auto& image : images) {
        result = std::max(result, image_peak(image.image));
    }
    return result;
}

[[nodiscard]] std::vector<float> float_storage(
    const Image& image)
{
    std::vector<float> result;
    result.reserve(
        image.pixels.size() *
        environment_float_channels);
    for (const auto pixel : image.pixels) {
        result.push_back(pixel.red);
        result.push_back(pixel.green);
        result.push_back(pixel.blue);
        result.push_back(pixel.alpha);
    }
    return result;
}

} // namespace

EnvironmentLighting::EnvironmentLighting(
    EnvironmentLightingMetadata metadata,
    std::vector<CubeSubresourceStorage> radiance,
    std::vector<CubeSubresourceStorage> irradiance,
    std::vector<CubeSubresourceStorage> specular,
    Texture2DStorage brdf_lut) noexcept
    : metadata_(metadata)
    , radiance_(std::move(radiance))
    , irradiance_(std::move(irradiance))
    , specular_(std::move(specular))
    , brdf_lut_(std::move(brdf_lut))
{
}

const EnvironmentLightingMetadata& EnvironmentLighting::metadata()
    const noexcept
{
    return metadata_;
}

std::size_t EnvironmentLighting::radiance_subresource_count()
    const noexcept
{
    return radiance_.size();
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::radiance_subresource(
    const std::size_t index) const noexcept
{
    if (index >= radiance_.size()) {
        return std::nullopt;
    }
    const auto& stored = radiance_[index];
    const auto bytes = std::as_bytes(std::span<const float>{
        stored.texels.data(),
        stored.texels.size(),
    });
    return EnvironmentCubeSubresourceView{
        .face = stored.face,
        .mip_level = stored.mip_level,
        .width = stored.width,
        .height = stored.height,
        .row_pitch =
            static_cast<std::size_t>(stored.width) *
            environment_texel_bytes,
        .slice_pitch = bytes.size(),
        .pixels = bytes,
    };
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::radiance_subresource(
    const CubemapFace face,
    const std::uint32_t mip_level) const noexcept
{
    const auto face_index = static_cast<std::size_t>(face);
    if (face_index >= cubemap_face_count ||
        mip_level >= environment_radiance_mip_levels) {
        return std::nullopt;
    }
    return radiance_subresource(
        face_index * environment_radiance_mip_levels +
        mip_level);
}

std::size_t EnvironmentLighting::irradiance_subresource_count()
    const noexcept
{
    return irradiance_.size();
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::irradiance_subresource(
    const std::size_t index) const noexcept
{
    if (index >= irradiance_.size()) {
        return std::nullopt;
    }
    const auto& stored = irradiance_[index];
    const auto bytes = std::as_bytes(std::span<const float>{
        stored.texels.data(),
        stored.texels.size(),
    });
    return EnvironmentCubeSubresourceView{
        .face = stored.face,
        .mip_level = stored.mip_level,
        .width = stored.width,
        .height = stored.height,
        .row_pitch =
            static_cast<std::size_t>(stored.width) *
            environment_texel_bytes,
        .slice_pitch = bytes.size(),
        .pixels = bytes,
    };
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::irradiance_subresource(
    const CubemapFace face,
    const std::uint32_t mip_level) const noexcept
{
    const auto face_index = static_cast<std::size_t>(face);
    if (face_index >= cubemap_face_count ||
        mip_level >= environment_irradiance_mip_levels) {
        return std::nullopt;
    }
    return irradiance_subresource(
        face_index * environment_irradiance_mip_levels +
        mip_level);
}

std::size_t EnvironmentLighting::specular_subresource_count()
    const noexcept
{
    return specular_.size();
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::specular_subresource(
    const std::size_t index) const noexcept
{
    if (index >= specular_.size()) {
        return std::nullopt;
    }
    const auto& stored = specular_[index];
    const auto bytes = std::as_bytes(std::span<const float>{
        stored.texels.data(),
        stored.texels.size(),
    });
    return EnvironmentCubeSubresourceView{
        .face = stored.face,
        .mip_level = stored.mip_level,
        .width = stored.width,
        .height = stored.height,
        .row_pitch =
            static_cast<std::size_t>(stored.width) *
            environment_texel_bytes,
        .slice_pitch = bytes.size(),
        .pixels = bytes,
    };
}

std::optional<EnvironmentCubeSubresourceView>
EnvironmentLighting::specular_subresource(
    const CubemapFace face,
    const std::uint32_t mip_level) const noexcept
{
    const auto face_index = static_cast<std::size_t>(face);
    if (face_index >= cubemap_face_count ||
        mip_level >= environment_specular_mip_levels) {
        return std::nullopt;
    }
    return specular_subresource(
        face_index * environment_specular_mip_levels +
        mip_level);
}

EnvironmentTexture2DView EnvironmentLighting::brdf_lut()
    const noexcept
{
    const auto bytes = std::as_bytes(std::span<const float>{
        brdf_lut_.texels.data(),
        brdf_lut_.texels.size(),
    });
    return {
        .width = brdf_lut_.width,
        .height = brdf_lut_.height,
        .mip_level = 0,
        .row_pitch =
            static_cast<std::size_t>(brdf_lut_.width) *
            environment_texel_bytes,
        .slice_pitch = bytes.size(),
        .pixels = bytes,
    };
}

core::Result<EnvironmentLighting>
generate_deterministic_environment_lighting()
{
    auto source = generate_source();
    if (!valid_image(source)) {
        return core::Result<EnvironmentLighting>::failure(
            environment_error(
                "Generated HDR latitude-longitude source is invalid"));
    }
    const auto source_peak = image_peak(source);
    if (!std::isfinite(source_peak) ||
        source_peak <= 1.0F) {
        return core::Result<EnvironmentLighting>::failure(
            environment_error(
                "Generated environment does not contain HDR radiance"));
    }

    auto radiance = build_radiance_cube(source);
    const auto source_samples = build_source_samples(source);
    auto irradiance = build_irradiance_cube(source_samples);
    auto specular = build_specular_cube(source, radiance);
    auto brdf_lut = build_brdf_lut();
    if (!valid_cube_images(
            radiance,
            environment_radiance_dimension,
            environment_radiance_mip_levels) ||
        !valid_cube_images(
            irradiance,
            environment_irradiance_dimension,
            environment_irradiance_mip_levels) ||
        !valid_cube_images(
            specular,
            environment_specular_dimension,
            environment_specular_mip_levels) ||
        !valid_image(brdf_lut)) {
        return core::Result<EnvironmentLighting>::failure(
            environment_error(
                "Generated environment-lighting maps are invalid"));
    }

    const auto radiance_peak = cube_peak(radiance);
    const auto irradiance_peak = cube_peak(irradiance);
    const auto specular_peak = cube_peak(specular);
    const auto brdf_peak = image_peak(brdf_lut);
    const auto derived_peak = std::max({
        radiance_peak,
        irradiance_peak,
        specular_peak,
        brdf_peak,
    });

    std::vector<EnvironmentLighting::CubeSubresourceStorage>
        radiance_storage;
    std::vector<EnvironmentLighting::CubeSubresourceStorage>
        irradiance_storage;
    std::vector<EnvironmentLighting::CubeSubresourceStorage>
        specular_storage;
    const auto convert_cube = [](
        const std::vector<CubeImage>& source_images,
        std::vector<
            EnvironmentLighting::CubeSubresourceStorage>& destination) {
        destination.reserve(source_images.size());
        for (const auto& image : source_images) {
            destination.push_back({
                .face = image.face,
                .mip_level = image.mip_level,
                .width = image.image.width,
                .height = image.image.height,
                .texels = float_storage(image.image),
            });
        }
    };
    convert_cube(radiance, radiance_storage);
    convert_cube(irradiance, irradiance_storage);
    convert_cube(specular, specular_storage);

    return core::Result<EnvironmentLighting>::success(
        EnvironmentLighting{
            EnvironmentLightingMetadata{
                .source_texel_count =
                    environment_source_texel_count,
                .source_byte_count =
                    environment_source_bytes,
                .derived_texel_count =
                    environment_derived_texel_count,
                .derived_subresource_count =
                    environment_derived_subresource_count,
                .derived_byte_count =
                    environment_derived_bytes,
                .source_peak_radiance = source_peak,
                .radiance_peak = radiance_peak,
                .diffuse_irradiance_peak = irradiance_peak,
                .specular_peak = specular_peak,
                .brdf_lut_peak = brdf_peak,
                .derived_peak_value = derived_peak,
            },
            std::move(radiance_storage),
            std::move(irradiance_storage),
            std::move(specular_storage),
            EnvironmentLighting::Texture2DStorage{
                .width = brdf_lut.width,
                .height = brdf_lut.height,
                .texels = float_storage(brdf_lut),
            },
        });
}

} // namespace shark::assets
