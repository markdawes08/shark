#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

namespace shark::core {

enum class AssertionKind : std::uint8_t {
    debug_assertion = 1,
    invariant,
};

struct AssertionFailure final {
    AssertionKind kind;
    std::string_view expression;
    std::string_view message;
    std::source_location location;
};

using AssertionHandler = void (*)(const AssertionFailure& failure);

[[nodiscard]] AssertionHandler set_assertion_handler(
    AssertionHandler handler) noexcept;

void reset_assertion_handler() noexcept;

[[noreturn]] void report_assertion_failure(
    AssertionKind kind,
    std::string_view expression,
    std::string_view message,
    std::source_location location);

} // namespace shark::core

#define SHARK_DETAIL_ASSERTION(kind, condition, message)                       \
    (static_cast<bool>(condition)                                              \
         ? static_cast<void>(0)                                                \
         : ::shark::core::report_assertion_failure(                            \
               kind,                                                          \
               #condition,                                                    \
               std::string_view{message},                                     \
               std::source_location::current()))

#define SHARK_ENSURE(condition, message)                                       \
    SHARK_DETAIL_ASSERTION(                                                    \
        ::shark::core::AssertionKind::invariant, condition, message)

#if defined(NDEBUG)
#define SHARK_ASSERT(condition, message)                                       \
    static_cast<void>(                                                        \
        sizeof(static_cast<bool>(condition)) + sizeof(std::string_view{message}))
#else
#define SHARK_ASSERT(condition, message)                                       \
    SHARK_DETAIL_ASSERTION(                                                    \
        ::shark::core::AssertionKind::debug_assertion, condition, message)
#endif
