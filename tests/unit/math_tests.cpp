#include <shark/core/math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE(
    "row-vector matrix composition applies the left transform first",
    "[math][matrix]")
{
    using namespace shark;

    const math::Matrix4x4 translation{{
        {1.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 0.0F},
        {2.0F, 3.0F, 4.0F, 1.0F},
    }};
    const math::Matrix4x4 scale{{
        {2.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 3.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 4.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F},
    }};
    constexpr math::Float4 point{1.0F, 2.0F, 3.0F, 1.0F};

    const auto sequential = math::transform(
        math::transform(point, translation),
        scale);
    const auto composed = math::transform(
        point,
        math::multiply(translation, scale));

    REQUIRE(composed.x == Catch::Approx(sequential.x));
    REQUIRE(composed.y == Catch::Approx(sequential.y));
    REQUIRE(composed.z == Catch::Approx(sequential.z));
    REQUIRE(composed.w == Catch::Approx(sequential.w));
    REQUIRE(composed.x == Catch::Approx(6.0F));
    REQUIRE(composed.y == Catch::Approx(15.0F));
    REQUIRE(composed.z == Catch::Approx(28.0F));
}

TEST_CASE(
    "identity matrices and finite checks preserve the POD contract",
    "[math][matrix]")
{
    using namespace shark;

    constexpr math::Float4 value{1.0F, -2.0F, 3.0F, 1.0F};
    const auto identity = math::identity_matrix();
    const auto transformed = math::transform(value, identity);

    REQUIRE(transformed == value);
    REQUIRE(math::is_finite(identity));
    REQUIRE(math::is_finite(math::Float3{1.0F, 2.0F, 3.0F}));
    REQUIRE(math::is_finite(value));

    auto non_finite = identity;
    non_finite.elements[2][1] =
        std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(math::is_finite(non_finite));
    REQUIRE_FALSE(math::is_finite(math::Float3{
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        0.0F,
    }));
}
