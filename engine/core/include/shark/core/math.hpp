#pragma once

#include <cstddef>
#include <type_traits>

namespace shark::math {

inline constexpr float pi = 3.14159265358979323846F;
inline constexpr float half_pi = pi * 0.5F;
inline constexpr float two_pi = pi * 2.0F;

struct Float3 final {
    float x{};
    float y{};
    float z{};

    [[nodiscard]] friend bool operator==(
        const Float3&,
        const Float3&) = default;
};

struct Float4 final {
    float x{};
    float y{};
    float z{};
    float w{};

    [[nodiscard]] friend bool operator==(
        const Float4&,
        const Float4&) = default;
};

struct Quaternion final {
    float x{};
    float y{};
    float z{};
    float w{1.0F};

    [[nodiscard]] friend bool operator==(
        const Quaternion&,
        const Quaternion&) = default;
};

static_assert(std::is_standard_layout_v<Quaternion>);
static_assert(std::is_trivially_copyable_v<Quaternion>);

struct alignas(16) Matrix4x4 final {
    float elements[4][4]{};
};

static_assert(sizeof(Matrix4x4) == 64);
static_assert(alignof(Matrix4x4) == 16);
static_assert(std::is_standard_layout_v<Matrix4x4>);
static_assert(std::is_trivially_copyable_v<Matrix4x4>);

[[nodiscard]] Matrix4x4 identity_matrix() noexcept;

[[nodiscard]] Matrix4x4 multiply(
    const Matrix4x4& left,
    const Matrix4x4& right) noexcept;

[[nodiscard]] Float4 transform(
    Float4 vector,
    const Matrix4x4& matrix) noexcept;

[[nodiscard]] bool is_finite(Float3 value) noexcept;
[[nodiscard]] bool is_finite(Float4 value) noexcept;
[[nodiscard]] bool is_finite(Quaternion value) noexcept;
[[nodiscard]] bool is_normalized(
    Quaternion value,
    float tolerance = 0.00001F) noexcept;
[[nodiscard]] bool is_unit(
    Quaternion value,
    float tolerance = 0.00001F) noexcept;
[[nodiscard]] bool is_finite(const Matrix4x4& value) noexcept;

} // namespace shark::math
