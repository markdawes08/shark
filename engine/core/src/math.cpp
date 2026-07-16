#include <shark/core/math.hpp>

#include <DirectXMath.h>

#include <cmath>

namespace shark::math {
namespace {

[[nodiscard]] DirectX::XMMATRIX load_matrix(
    const Matrix4x4& matrix) noexcept
{
    return DirectX::XMMatrixSet(
        matrix.elements[0][0],
        matrix.elements[0][1],
        matrix.elements[0][2],
        matrix.elements[0][3],
        matrix.elements[1][0],
        matrix.elements[1][1],
        matrix.elements[1][2],
        matrix.elements[1][3],
        matrix.elements[2][0],
        matrix.elements[2][1],
        matrix.elements[2][2],
        matrix.elements[2][3],
        matrix.elements[3][0],
        matrix.elements[3][1],
        matrix.elements[3][2],
        matrix.elements[3][3]);
}

[[nodiscard]] Matrix4x4 store_matrix(
    const DirectX::XMMATRIX matrix) noexcept
{
    DirectX::XMFLOAT4X4 stored{};
    DirectX::XMStoreFloat4x4(&stored, matrix);

    return Matrix4x4{{
        {stored._11, stored._12, stored._13, stored._14},
        {stored._21, stored._22, stored._23, stored._24},
        {stored._31, stored._32, stored._33, stored._34},
        {stored._41, stored._42, stored._43, stored._44},
    }};
}

} // namespace

Matrix4x4 identity_matrix() noexcept
{
    return store_matrix(DirectX::XMMatrixIdentity());
}

Matrix4x4 multiply(
    const Matrix4x4& left,
    const Matrix4x4& right) noexcept
{
    return store_matrix(DirectX::XMMatrixMultiply(
        load_matrix(left),
        load_matrix(right)));
}

Float4 transform(
    const Float4 vector,
    const Matrix4x4& matrix) noexcept
{
    const auto transformed = DirectX::XMVector4Transform(
        DirectX::XMVectorSet(
            vector.x,
            vector.y,
            vector.z,
            vector.w),
        load_matrix(matrix));

    DirectX::XMFLOAT4 stored{};
    DirectX::XMStoreFloat4(&stored, transformed);
    return Float4{stored.x, stored.y, stored.z, stored.w};
}

bool is_finite(const Float3 value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool is_finite(const Float4 value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z) &&
        std::isfinite(value.w);
}

bool is_finite(const Matrix4x4& value) noexcept
{
    for (const auto& row : value.elements) {
        for (const auto element : row) {
            if (!std::isfinite(element)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace shark::math
