#include <shark/core/logging.hpp>

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace shark::core {
namespace {

struct LoggingState final {
    std::shared_ptr<std::ostream> output_owner;
    std::shared_ptr<spdlog::logger> backend;
};

using LoggingStatePointer = std::shared_ptr<LoggingState>;

std::atomic<LoggingStatePointer> logging_state;

[[nodiscard]] spdlog::level::level_enum to_spdlog_level(
    const LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::trace:
        return spdlog::level::trace;
    case LogLevel::debug:
        return spdlog::level::debug;
    case LogLevel::info:
        return spdlog::level::info;
    case LogLevel::warning:
        return spdlog::level::warn;
    case LogLevel::error:
        return spdlog::level::err;
    case LogLevel::critical:
        return spdlog::level::critical;
    case LogLevel::off:
        return spdlog::level::off;
    }

    return spdlog::level::off;
}

[[nodiscard]] Result<void> logging_failure(std::string message)
{
    return Result<void>::failure(Error{
        ErrorCategory::core,
        ErrorCode::operation_failed,
        std::move(message),
    });
}

} // namespace

Result<void> initialize_logging(const LoggingConfig& config)
{
    if (logging_state.load(std::memory_order_acquire) != nullptr) {
        return Result<void>::failure(Error{
            ErrorCategory::core,
            ErrorCode::invalid_state,
            "Shark logging is already initialized",
        });
    }

    try {
        std::shared_ptr<spdlog::sinks::sink> sink;
        if (config.output_override) {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(
                *config.output_override,
                true);
        }
        else {
            sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        }

        auto candidate = std::make_shared<spdlog::logger>("shark", sink);
        candidate->set_pattern(
            "[%Y-%m-%dT%H:%M:%S.%e] [%t] %^[%l]%$ [%n] [%s:%# %!] %v");
        candidate->set_level(to_spdlog_level(config.minimum_level));
        candidate->flush_on(spdlog::level::err);
        candidate->set_error_handler([](const std::string& message) {
            std::fprintf(
                stderr,
                "Shark logging backend error: %s\n",
                message.c_str());
        });

        auto candidate_state = std::make_shared<LoggingState>(LoggingState{
            .output_owner = config.output_override,
            .backend = std::move(candidate),
        });

        LoggingStatePointer expected;
        if (!logging_state.compare_exchange_strong(
                expected,
                candidate_state,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return Result<void>::failure(Error{
                ErrorCategory::core,
                ErrorCode::invalid_state,
                "Shark logging was initialized concurrently",
            });
        }

        return Result<void>::success();
    }
    catch (const std::exception& exception) {
        std::string message{"Failed to initialize Shark logging: "};
        message.append(exception.what());
        return logging_failure(std::move(message));
    }
    catch (...) {
        return logging_failure("Failed to initialize Shark logging");
    }
}

void shutdown_logging() noexcept
{
    auto detached =
        logging_state.exchange(nullptr, std::memory_order_acq_rel);
    if (detached == nullptr) {
        return;
    }

    try {
        detached->backend->flush();
    }
    catch (...) {
        std::fputs("Shark logging flush failed during shutdown\n", stderr);
    }
}

void flush_logging() noexcept
{
    const auto active = logging_state.load(std::memory_order_acquire);
    if (active == nullptr) {
        return;
    }

    try {
        active->backend->flush();
    }
    catch (...) {
        std::fputs("Shark logging flush failed\n", stderr);
    }
}

bool logging_is_initialized() noexcept
{
    return logging_state.load(std::memory_order_acquire) != nullptr;
}

void log_message(
    const LogLevel level,
    const std::string_view category,
    const std::string_view message,
    const std::source_location location) noexcept
{
    if (level == LogLevel::off) {
        return;
    }

    const auto active = logging_state.load(std::memory_order_acquire);
    if (active == nullptr) {
        return;
    }

    try {
        active->backend->log(
            spdlog::source_loc{
                location.file_name(),
                static_cast<int>(location.line()),
                location.function_name(),
            },
            to_spdlog_level(level),
            "[{}] {}",
            category,
            message);
    }
    catch (...) {
        std::fputs("Shark failed to write a log record\n", stderr);
    }
}

} // namespace shark::core
