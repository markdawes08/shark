#include <shark/core/assertion.hpp>

#include <shark/core/logging.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace shark::core {
namespace {

void write_view(const std::string_view text) noexcept
{
    if (!text.empty()) {
        static_cast<void>(
            std::fwrite(text.data(), sizeof(char), text.size(), stderr));
    }
}

[[nodiscard]] const char* assertion_kind_name(
    const AssertionKind kind) noexcept
{
    switch (kind) {
    case AssertionKind::debug_assertion:
        return "debug assertion";
    case AssertionKind::invariant:
        return "invariant";
    }

    return "assertion";
}

[[noreturn]] void default_assertion_handler(
    const AssertionFailure& failure)
{
    log_message(
        LogLevel::critical,
        "assertion",
        failure.message,
        failure.location);
    flush_logging();

    std::fputs("Shark ", stderr);
    std::fputs(assertion_kind_name(failure.kind), stderr);
    std::fputs(" failed: ", stderr);
    write_view(failure.expression);
    if (!failure.message.empty()) {
        std::fputs(" | ", stderr);
        write_view(failure.message);
    }
    std::fprintf(
        stderr,
        " at %s:%lu (%s)\n",
        failure.location.file_name(),
        static_cast<unsigned long>(failure.location.line()),
        failure.location.function_name());
    std::fflush(stderr);
    std::abort();
}

std::atomic<AssertionHandler> assertion_handler{&default_assertion_handler};

} // namespace

AssertionHandler set_assertion_handler(
    const AssertionHandler handler) noexcept
{
    const auto replacement =
        handler != nullptr ? handler : &default_assertion_handler;
    return assertion_handler.exchange(replacement, std::memory_order_acq_rel);
}

void reset_assertion_handler() noexcept
{
    assertion_handler.store(
        &default_assertion_handler,
        std::memory_order_release);
}

[[noreturn]] void report_assertion_failure(
    const AssertionKind kind,
    const std::string_view expression,
    const std::string_view message,
    const std::source_location location)
{
    const AssertionFailure failure{kind, expression, message, location};
    const auto handler = assertion_handler.load(std::memory_order_acquire);
    handler(failure);

    std::fputs("Shark assertion handler returned unexpectedly\n", stderr);
    std::fflush(stderr);
    std::abort();
}

} // namespace shark::core
