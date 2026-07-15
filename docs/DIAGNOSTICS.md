# Shark Core Diagnostics

- **Increment:** `F-003`
- **Last verified:** July 15, 2026

This increment establishes the failure and diagnostic contracts used by later
platform, graphics, and simulation systems. The public boundary contains no
spdlog types and has no Win32 or Direct3D dependency.

## Recoverable failures

`Error` owns three fields: an `ErrorCategory`, an `ErrorCode`, and a diagnostic
message. Owning the message allows an error to cross stack frames safely. Errors
do not log themselves; the caller that decides a failure is terminal is
responsible for making it observable, which prevents duplicate records.

`Result<T>` and `Result<void>` represent either success or one `Error`. They are
`[[nodiscard]]`, use explicit `success` and `failure` factories, support
move-only values, and require callers to check the state before accessing the
active value or error. Accessing the wrong state violates an always-on invariant
and terminates through the assertion path. Result assignment is intentionally
disabled so a throwing payload assignment cannot leave the success/error state
valueless; construction and copy/move construction remain available when the
payload supports them.

Normal operational failures therefore use explicit control flow rather than
exceptions:

```cpp
auto result = initialize_subsystem();
if (!result) {
    return Result<void>::failure(std::move(result).error());
}
```

Native error payloads, nested causes, stack traces, and monadic helpers wait
until a concrete subsystem demonstrates the need.

## Structured logging

`initialize_logging` creates one Shark-owned synchronous spdlog logger. It is
not registered as spdlog's default logger and shutdown affects no logger owned
by an embedding application. Each record contains a timestamp, thread ID,
severity, logger name, category, message, file, line, and function.

Logging rules:

- initialize once and check the returned `Result<void>`;
- a duplicate or concurrent initialization returns `ErrorCode::invalid_state`;
- calls before initialization or after shutdown are safe no-ops;
- logging calls retain the backend safely while a concurrent shutdown detaches
  it;
- error and critical records flush automatically, and explicit flush/shutdown
  never throw;
- the default minimum level is Debug in a Debug build and Info in Release; and
- `LoggingConfig::output_override` is an optional shared output stream for tests
  and embedding hosts. The retained owner keeps it alive through racing log
  calls and shutdown.

The backend is synchronous by design. Asynchronous logging and its global
thread pool are deferred until measurements justify the extra lifecycle.

## Assertions and invariants

Two macros make Release behavior explicit:

| Macro | Debug | Release | Intended use |
|---|---|---|---|
| `SHARK_ENSURE(condition, message)` | Evaluated | Evaluated | Nonrecoverable invariants and API preconditions |
| `SHARK_ASSERT(condition, message)` | Evaluated | Type-checked but not evaluated | Expensive development-only validation |

The active condition is evaluated exactly once. A failure record contains its
kind, expression text, message, and `std::source_location`. The production
handler writes a critical log record when logging is available, always emits a
raw stderr fallback, flushes, and aborts.

Tests may install a scoped handler. The controlled F-003 test uses a handler
that captures the record and throws a private sentinel; the reporting function
aborts if any handler returns. Production code must never use a returning or
recovering assertion handler. The record's string views are callback-lifetime
data; a handler that retains a record must copy them.

## Verification

Catch2 discovers each unit case as an independent CTest test. Permanent F-003
coverage includes owned error messages, value/error/void and move-only results,
wrong-state result access, logging lifecycle and structured source fields,
concurrent record serialization, an intentional invariant failure, a
non-failing invariant, and Debug/Release assertion evaluation.

All Shark-owned targets compile with MSVC `/W4 /WX`. Imported vcpkg headers
remain external with their warnings disabled; the project does not suppress
warnings in its own code.
