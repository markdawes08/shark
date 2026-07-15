#include <shark/core/error.hpp>
#include <shark/core/result.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using shark::core::Error;
using shark::core::ErrorCategory;
using shark::core::ErrorCode;
using shark::core::Result;

} // namespace

TEST_CASE("errors own their diagnostic data", "[core][result]")
{
    std::string source_message{"resource was not found"};
    const Error error{
        ErrorCategory::assets,
        ErrorCode::not_found,
        source_message,
    };
    source_message.assign("changed after construction");

    REQUIRE(error.category() == ErrorCategory::assets);
    REQUIRE(error.code() == ErrorCode::not_found);
    REQUIRE(error.message() == "resource was not found");
}

TEST_CASE("successful results expose their value", "[core][result]")
{
    auto result = Result<int>::success(41);
    REQUIRE(result.has_value());
    REQUIRE(static_cast<bool>(result));

    result.value() += 1;
    const auto& const_result = result;
    REQUIRE(const_result.value() == 42);

    const auto consumed = std::move(result).value();
    REQUIRE(consumed == 42);
}

TEST_CASE("failed results preserve structured errors", "[core][result]")
{
    auto result = Result<int>::failure(Error{
        ErrorCategory::platform,
        ErrorCode::unavailable,
        "timer unavailable",
    });

    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(static_cast<bool>(result));
    REQUIRE(result.error().category() == ErrorCategory::platform);
    REQUIRE(result.error().code() == ErrorCode::unavailable);
    REQUIRE(result.error().message() == "timer unavailable");

    const auto moved_error = std::move(result).error();
    REQUIRE(moved_error.message() == "timer unavailable");
}

TEST_CASE("results support move-only, void, and Error payloads", "[core][result]")
{
    using MoveOnlyResult = Result<std::unique_ptr<int>>;
    static_assert(std::is_move_constructible_v<MoveOnlyResult>);
    static_assert(!std::is_copy_constructible_v<MoveOnlyResult>);
    static_assert(!std::is_copy_assignable_v<Result<int>>);
    static_assert(!std::is_move_assignable_v<Result<int>>);

    auto move_only = MoveOnlyResult::success(std::make_unique<int>(7));
    const auto value = std::move(move_only).value();
    REQUIRE(*value == 7);

    const auto void_success = Result<void>::success();
    REQUIRE(void_success.has_value());
    void_success.value();

    const auto void_failure = Result<void>::failure(Error{
        ErrorCategory::core,
        ErrorCode::operation_failed,
        "operation failed",
    });
    REQUIRE_FALSE(void_failure.has_value());
    REQUIRE(void_failure.error().code() == ErrorCode::operation_failed);

    const Error payload{
        ErrorCategory::graphics,
        ErrorCode::unsupported,
        "payload value",
    };
    const auto error_payload = Result<Error>::success(payload);
    REQUIRE(error_payload.value().category() == ErrorCategory::graphics);

    const auto owned_rvalue_message =
        Result<void>::failure(Error{
            ErrorCategory::core,
            ErrorCode::operation_failed,
            "safe rvalue message",
        })
            .error()
            .message();
    REQUIRE(owned_rvalue_message == "safe rvalue message");
}
