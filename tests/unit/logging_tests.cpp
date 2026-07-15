#include <shark/core/error.hpp>
#include <shark/core/logging.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

class LoggingSession final {
public:
    LoggingSession() = default;
    ~LoggingSession()
    {
        shark::core::shutdown_logging();
    }

    LoggingSession(const LoggingSession&) = delete;
    LoggingSession& operator=(const LoggingSession&) = delete;
    LoggingSession(LoggingSession&&) = delete;
    LoggingSession& operator=(LoggingSession&&) = delete;
};

} // namespace

TEST_CASE("logging has an explicit lifecycle", "[core][logging]")
{
    using namespace shark::core;

    shutdown_logging();
    const auto output = std::make_shared<std::ostringstream>();
    const LoggingSession session;
    const LoggingConfig config{
        .minimum_level = LogLevel::trace,
        .output_override = output,
    };

    const auto initialized = initialize_logging(config);
    REQUIRE(initialized.has_value());
    REQUIRE(logging_is_initialized());

    const auto duplicate = initialize_logging(config);
    REQUIRE_FALSE(duplicate.has_value());
    REQUIRE(duplicate.error().category() == ErrorCategory::core);
    REQUIRE(duplicate.error().code() == ErrorCode::invalid_state);

    shutdown_logging();
    REQUIRE_FALSE(logging_is_initialized());
    shutdown_logging();
}

TEST_CASE("logging writes structured source-aware records", "[core][logging]")
{
    using namespace shark::core;

    shutdown_logging();
    const auto output = std::make_shared<std::ostringstream>();
    const LoggingSession session;
    const LoggingConfig config{
        .minimum_level = LogLevel::trace,
        .output_override = output,
    };
    REQUIRE(initialize_logging(config).has_value());

    const auto source_line = __LINE__ + 1;
    log_message(LogLevel::warning, "unit", "structured message");
    flush_logging();

    const std::string record = output->str();
    REQUIRE(record.find("[warning]") != std::string::npos);
    REQUIRE(record.find("[shark]") != std::string::npos);
    REQUIRE(record.find("[unit] structured message") != std::string::npos);
    REQUIRE(record.find("logging_tests.cpp") != std::string::npos);
    REQUIRE(record.find(":" + std::to_string(source_line)) != std::string::npos);

    shutdown_logging();
    const auto size_after_shutdown = output->str().size();
    log_message(LogLevel::critical, "unit", "ignored after shutdown");
    REQUIRE(output->str().size() == size_after_shutdown);
}

TEST_CASE("logging serializes concurrent records", "[core][logging]")
{
    using namespace shark::core;

    shutdown_logging();
    const auto output = std::make_shared<std::ostringstream>();
    const LoggingSession session;
    const LoggingConfig config{
        .minimum_level = LogLevel::trace,
        .output_override = output,
    };
    REQUIRE(initialize_logging(config).has_value());

    constexpr int worker_count = 4;
    constexpr int records_per_worker = 25;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([] {
            for (int record = 0; record < records_per_worker; ++record) {
                log_message(
                    LogLevel::info,
                    "thread",
                    "concurrent record");
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    flush_logging();

    const std::string records = output->str();
    constexpr std::string_view marker{"[thread] concurrent record"};
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = records.find(marker, position)) != std::string::npos) {
        ++count;
        position += marker.size();
    }
    REQUIRE(count == worker_count * records_per_worker);
}
