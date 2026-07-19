#include "terrain_frustum_culling.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace shark::renderer::detail {
namespace {

inline constexpr double bounds_plane_epsilon = 0.0001;

struct Double4 final {
    double x{};
    double y{};
    double z{};
    double w{};
};

[[nodiscard]] constexpr Double4 add(
    const Double4 left,
    const Double4 right) noexcept
{
    return Double4{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
        left.w + right.w,
    };
}

[[nodiscard]] constexpr Double4 subtract(
    const Double4 left,
    const Double4 right) noexcept
{
    return Double4{
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
        left.w - right.w,
    };
}

[[nodiscard]] std::optional<FrustumPlane> normalized_plane(
    const Double4 coefficients) noexcept
{
    if (!std::isfinite(coefficients.x) ||
        !std::isfinite(coefficients.y) ||
        !std::isfinite(coefficients.z) ||
        !std::isfinite(coefficients.w)) {
        return std::nullopt;
    }

    const auto x = coefficients.x;
    const auto y = coefficients.y;
    const auto z = coefficients.z;
    const auto distance = coefficients.w;
    const auto scale = std::max({
        std::abs(x),
        std::abs(y),
        std::abs(z),
    });
    if (!std::isfinite(scale) || scale == 0.0) {
        return std::nullopt;
    }
    const auto scaled_x = x / scale;
    const auto scaled_y = y / scale;
    const auto scaled_z = z / scale;
    const auto scaled_length = std::sqrt(
        scaled_x * scaled_x +
        scaled_y * scaled_y +
        scaled_z * scaled_z);
    if (!std::isfinite(scaled_length) ||
        scaled_length == 0.0) {
        return std::nullopt;
    }

    const FrustumPlane plane{
        {
            static_cast<float>(scaled_x / scaled_length),
            static_cast<float>(scaled_y / scaled_length),
            static_cast<float>(scaled_z / scaled_length),
        },
        (distance / scale) / scaled_length,
    };
    if (!math::is_finite(plane.normal) ||
        !std::isfinite(plane.distance)) {
        return std::nullopt;
    }
    return plane;
}

[[nodiscard]] bool has_full_rank(
    const math::Matrix4x4& matrix) noexcept
{
    double elements[4][4]{};
    double scale = 0.0;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            elements[row][column] =
                static_cast<double>(
                    matrix.elements[row][column]);
            scale = std::max(
                scale,
                std::abs(elements[row][column]));
        }
    }
    if (!std::isfinite(scale) || scale == 0.0) {
        return false;
    }
    for (auto& row : elements) {
        for (auto& element : row) {
            element /= scale;
        }
    }

    for (std::size_t column = 0; column < 4; ++column) {
        auto pivot_row = column;
        auto pivot_magnitude =
            std::abs(elements[pivot_row][column]);
        for (std::size_t row = column + 1U;
             row < 4;
             ++row) {
            const auto magnitude =
                std::abs(elements[row][column]);
            if (magnitude > pivot_magnitude) {
                pivot_row = row;
                pivot_magnitude = magnitude;
            }
        }
        if (!std::isfinite(pivot_magnitude) ||
            pivot_magnitude == 0.0) {
            return false;
        }
        if (pivot_row != column) {
            std::swap(elements[pivot_row], elements[column]);
        }

        const auto pivot = elements[column][column];
        for (std::size_t row = column + 1U;
             row < 4;
             ++row) {
            const auto factor = elements[row][column] / pivot;
            for (std::size_t remaining = column;
                 remaining < 4;
                 ++remaining) {
                elements[row][remaining] -=
                    factor * elements[column][remaining];
            }
        }
    }
    return true;
}

} // namespace

std::optional<ViewFrustum> make_view_frustum(
    const math::Matrix4x4& view_projection) noexcept
{
    if (!math::is_finite(view_projection) ||
        !has_full_rank(view_projection)) {
        return std::nullopt;
    }

    const auto column = [&view_projection](
                            const std::size_t index) noexcept {
        return Double4{
            static_cast<double>(
                view_projection.elements[0][index]),
            static_cast<double>(
                view_projection.elements[1][index]),
            static_cast<double>(
                view_projection.elements[2][index]),
            static_cast<double>(
                view_projection.elements[3][index]),
        };
    };
    const auto x = column(0);
    const auto y = column(1);
    const auto z = column(2);
    const auto w = column(3);
    const std::array coefficients{
        add(w, x),
        subtract(w, x),
        add(w, y),
        subtract(w, y),
        z,
        subtract(w, z),
    };

    ViewFrustum frustum{};
    for (std::size_t index = 0;
         index < coefficients.size();
         ++index) {
        const auto plane = normalized_plane(coefficients[index]);
        if (!plane.has_value()) {
            return std::nullopt;
        }
        frustum.planes[index] = *plane;
    }
    return frustum;
}

bool intersects_view_frustum(
    const ViewFrustum& frustum,
    const math::Float3 minimum,
    const math::Float3 maximum) noexcept
{
    for (const auto& plane : frustum.planes) {
        const math::Float3 support{
            plane.normal.x >= 0.0F ? maximum.x : minimum.x,
            plane.normal.y >= 0.0F ? maximum.y : minimum.y,
            plane.normal.z >= 0.0F ? maximum.z : minimum.z,
        };
        const auto distance =
            static_cast<double>(plane.normal.x) * support.x +
            static_cast<double>(plane.normal.y) * support.y +
            static_cast<double>(plane.normal.z) * support.z +
            plane.distance;
        if (distance < -bounds_plane_epsilon) {
            return false;
        }
    }
    return true;
}

} // namespace shark::renderer::detail
