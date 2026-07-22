#include <shark/core/math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

TEST_CASE(
    "scalar quaternion rotation preserves identity and sign equivalence",
    "[math][quaternion][rotation]")
{
    using namespace shark;

    constexpr math::Float3 vector{1.0F, 2.0F, 3.0F};
    REQUIRE(math::rotate(math::Quaternion{}, vector) == vector);

    const auto sine = std::sin(math::pi * 0.25F);
    const auto cosine = std::cos(math::pi * 0.25F);
    const math::Quaternion quarter_turn{
        0.0F,
        0.0F,
        sine,
        cosine,
    };
    const math::Quaternion equivalent_quarter_turn{
        -quarter_turn.x,
        -quarter_turn.y,
        -quarter_turn.z,
        -quarter_turn.w,
    };
    const auto rotated = math::rotate(quarter_turn, vector);
    const auto sign_equivalent = math::rotate(
        equivalent_quarter_turn,
        vector);

    REQUIRE(rotated.x == Catch::Approx(-2.0F).margin(0.000001F));
    REQUIRE(rotated.y == Catch::Approx(1.0F).margin(0.000001F));
    REQUIRE(rotated.z == Catch::Approx(3.0F).margin(0.000001F));
    REQUIRE(sign_equivalent.x == Catch::Approx(rotated.x));
    REQUIRE(sign_equivalent.y == Catch::Approx(rotated.y));
    REQUIRE(sign_equivalent.z == Catch::Approx(rotated.z));
}

TEST_CASE(
    "scalar quaternion rotation reports unrepresentable output",
    "[math][quaternion][rotation]")
{
    using namespace shark;

    const auto sine = std::sin(math::pi * 0.125F);
    const auto cosine = std::cos(math::pi * 0.125F);
    const auto maximum = std::numeric_limits<float>::max();
    const auto rotated = math::rotate(
        math::Quaternion{0.0F, 0.0F, sine, cosine},
        math::Float3{maximum, maximum, 0.0F});

    REQUIRE_FALSE(math::is_finite(rotated));
}
