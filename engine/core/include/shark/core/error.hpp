#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace shark::core {

enum class ErrorCategory : std::uint8_t {
    core = 1,
    platform = 2,
    graphics = 3,
    assets = 4,
    simulation = 5,
};

enum class ErrorCode : std::uint32_t {
    unknown = 1,
    invalid_argument = 2,
    not_found = 3,
    unsupported = 4,
    unavailable = 5,
    invalid_state = 6,
    operation_failed = 7,
};

class Error final {
public:
    Error(ErrorCategory category, ErrorCode code, std::string message);

    [[nodiscard]] ErrorCategory category() const noexcept;
    [[nodiscard]] ErrorCode code() const noexcept;
    [[nodiscard]] std::string_view message() const& noexcept;
    [[nodiscard]] std::string message() && noexcept;

private:
    ErrorCategory category_;
    ErrorCode code_;
    std::string message_;
};

} // namespace shark::core
