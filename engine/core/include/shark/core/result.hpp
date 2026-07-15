#pragma once

#include <shark/core/assertion.hpp>
#include <shark/core/error.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace shark::core {

template<typename T>
class [[nodiscard]] Result final {
    static_assert(!std::is_reference_v<T>, "Result cannot contain a reference");
    static_assert(!std::is_void_v<T>, "Use Result<void> for valueless success");
    static_assert(std::is_move_constructible_v<T>, "Result values must be movable");

    struct ValueState final {
        T value;
    };

    struct ErrorState final {
        Error error;
    };

public:
    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;
    ~Result() = default;

    [[nodiscard]] static Result success(T value)
    {
        return Result{ValueState{std::move(value)}};
    }

    [[nodiscard]] static Result failure(Error error)
    {
        return Result{ErrorState{std::move(error)}};
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return std::holds_alternative<ValueState>(state_);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] T& value() &
    {
        SHARK_ENSURE(has_value(), "Result::value() requires a successful result");
        return std::get<ValueState>(state_).value;
    }

    [[nodiscard]] const T& value() const&
    {
        SHARK_ENSURE(has_value(), "Result::value() requires a successful result");
        return std::get<ValueState>(state_).value;
    }

    [[nodiscard]] T&& value() &&
    {
        SHARK_ENSURE(has_value(), "Result::value() requires a successful result");
        return std::move(std::get<ValueState>(state_).value);
    }

    [[nodiscard]] const Error& error() const&
    {
        SHARK_ENSURE(
            std::holds_alternative<ErrorState>(state_),
            "Result::error() requires a failed result");
        return std::get<ErrorState>(state_).error;
    }

    [[nodiscard]] Error&& error() &&
    {
        SHARK_ENSURE(
            std::holds_alternative<ErrorState>(state_),
            "Result::error() requires a failed result");
        return std::move(std::get<ErrorState>(state_).error);
    }

private:
    explicit Result(ValueState value)
        : state_(std::in_place_type<ValueState>, std::move(value))
    {
    }

    explicit Result(ErrorState error)
        : state_(std::in_place_type<ErrorState>, std::move(error))
    {
    }

    std::variant<ValueState, ErrorState> state_;
};

template<>
class [[nodiscard]] Result<void> final {
    struct SuccessState final {
    };

    struct ErrorState final {
        Error error;
    };

public:
    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;
    ~Result() = default;

    [[nodiscard]] static Result success()
    {
        return Result{};
    }

    [[nodiscard]] static Result failure(Error error)
    {
        return Result{ErrorState{std::move(error)}};
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return std::holds_alternative<SuccessState>(state_);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    void value() const
    {
        SHARK_ENSURE(has_value(), "Result::value() requires a successful result");
    }

    [[nodiscard]] const Error& error() const&
    {
        SHARK_ENSURE(
            std::holds_alternative<ErrorState>(state_),
            "Result::error() requires a failed result");
        return std::get<ErrorState>(state_).error;
    }

    [[nodiscard]] Error&& error() &&
    {
        SHARK_ENSURE(
            std::holds_alternative<ErrorState>(state_),
            "Result::error() requires a failed result");
        return std::move(std::get<ErrorState>(state_).error);
    }

private:
    Result()
        : state_(std::in_place_type<SuccessState>)
    {
    }

    explicit Result(ErrorState error)
        : state_(std::in_place_type<ErrorState>, std::move(error))
    {
    }

    std::variant<SuccessState, ErrorState> state_;
};

} // namespace shark::core
