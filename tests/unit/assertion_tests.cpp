#include <shark/core/assertion.hpp>
#include <shark/core/error.hpp>
#include <shark/core/result.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct IntentionalAssertion final {
};

struct CapturedFailure final {
    shark::core::AssertionKind kind;
    std::string expression;
    std::string message;
    std::string file;
    std::uint_least32_t line;
};

std::optional<CapturedFailure> captured_failure;

void capture_and_throw(const shark::core::AssertionFailure& failure)
{
    captured_failure = CapturedFailure{
        .kind = failure.kind,
        .expression = std::string{failure.expression},
        .message = std::string{failure.message},
        .file = failure.location.file_name(),
        .line = failure.location.line(),
    };
    throw IntentionalAssertion{};
}

class AssertionHandlerScope final {
public:
    explicit AssertionHandlerScope(const shark::core::AssertionHandler handler)
        : previous_(shark::core::set_assertion_handler(handler))
    {
    }

    ~AssertionHandlerScope()
    {
        static_cast<void>(shark::core::set_assertion_handler(previous_));
    }

    AssertionHandlerScope(const AssertionHandlerScope&) = delete;
    AssertionHandlerScope& operator=(const AssertionHandlerScope&) = delete;
    AssertionHandlerScope(AssertionHandlerScope&&) = delete;
    AssertionHandlerScope& operator=(AssertionHandlerScope&&) = delete;

private:
    shark::core::AssertionHandler previous_;
};

} // namespace

TEST_CASE("an invariant failure is captured without weakening production", "[core][assertion]")
{
    using namespace shark::core;

    captured_failure.reset();
    const AssertionHandlerScope handler_scope{&capture_and_throw};

    const auto source_line = __LINE__ + 3;
    bool caught = false;
    try {
        SHARK_ENSURE(2 + 2 == 5, "intentional assertion test");
    }
    catch (const IntentionalAssertion&) {
        caught = true;
    }

    REQUIRE(caught);
    REQUIRE(captured_failure.has_value());
    REQUIRE(captured_failure->kind == AssertionKind::invariant);
    REQUIRE(captured_failure->expression == "2 + 2 == 5");
    REQUIRE(captured_failure->message == "intentional assertion test");
    REQUIRE(captured_failure->line == source_line);
    REQUIRE(std::string_view{captured_failure->file}.ends_with("assertion_tests.cpp"));
}

TEST_CASE("successful invariants do not invoke the assertion handler", "[core][assertion]")
{
    captured_failure.reset();
    const AssertionHandlerScope handler_scope{&capture_and_throw};

    SHARK_ENSURE(true, "must not fail");
    REQUIRE_FALSE(captured_failure.has_value());
}

TEST_CASE("debug assertions have configuration-safe evaluation", "[core][assertion]")
{
    captured_failure.reset();
    const AssertionHandlerScope handler_scope{&capture_and_throw};
    int evaluations = 0;

    SHARK_ASSERT(++evaluations == 1, "debug assertion should pass");

#if defined(NDEBUG)
    REQUIRE(evaluations == 0);
#else
    REQUIRE(evaluations == 1);
#endif
    REQUIRE_FALSE(captured_failure.has_value());
}

TEST_CASE("result wrong-state access uses always-on invariants", "[core][assertion][result]")
{
    using namespace shark::core;

    captured_failure.reset();
    const AssertionHandlerScope handler_scope{&capture_and_throw};

    auto success = Result<int>::success(1);
    bool caught_success_error = false;
    try {
        static_cast<void>(success.error());
    }
    catch (const IntentionalAssertion&) {
        caught_success_error = true;
    }
    REQUIRE(caught_success_error);
    REQUIRE(captured_failure.has_value());
    REQUIRE(captured_failure->kind == AssertionKind::invariant);
    REQUIRE(captured_failure->message == "Result::error() requires a failed result");

    captured_failure.reset();
    auto failure = Result<int>::failure(Error{
        ErrorCategory::core,
        ErrorCode::operation_failed,
        "expected failure",
    });
    bool caught_failure_value = false;
    try {
        static_cast<void>(failure.value());
    }
    catch (const IntentionalAssertion&) {
        caught_failure_value = true;
    }
    REQUIRE(caught_failure_value);
    REQUIRE(captured_failure.has_value());
    REQUIRE(captured_failure->kind == AssertionKind::invariant);
    REQUIRE(captured_failure->message == "Result::value() requires a successful result");
}
