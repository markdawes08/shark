#include <shark/core/error.hpp>

#include <utility>

namespace shark::core {

Error::Error(
    const ErrorCategory category,
    const ErrorCode code,
    std::string message)
    : category_(category)
    , code_(code)
    , message_(std::move(message))
{
}

ErrorCategory Error::category() const noexcept
{
    return category_;
}

ErrorCode Error::code() const noexcept
{
    return code_;
}

std::string_view Error::message() const& noexcept
{
    return message_;
}

std::string Error::message() && noexcept
{
    return std::move(message_);
}

} // namespace shark::core
