#pragma once

#include <shark/core/result.hpp>

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <source_location>
#include <string_view>

namespace shark::core {

enum class LogLevel : std::uint8_t {
    trace = 1,
    debug,
    info,
    warning,
    error,
    critical,
    off,
};

struct LoggingConfig final {
#if defined(NDEBUG)
    LogLevel minimum_level{LogLevel::info};
#else
    LogLevel minimum_level{LogLevel::debug};
#endif
    std::shared_ptr<std::ostream> output_override;
};

[[nodiscard]] Result<void> initialize_logging(
    const LoggingConfig& config = {});

void shutdown_logging() noexcept;
void flush_logging() noexcept;

[[nodiscard]] bool logging_is_initialized() noexcept;

void log_message(
    LogLevel level,
    std::string_view category,
    std::string_view message,
    std::source_location location = std::source_location::current()) noexcept;

} // namespace shark::core
