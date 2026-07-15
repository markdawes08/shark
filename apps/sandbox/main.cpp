#include <shark/core/engine_info.hpp>
#include <shark/core/logging.hpp>

#include <cstdlib>

int main()
{
    const auto logging = shark::core::initialize_logging();
    if (!logging) {
        return EXIT_FAILURE;
    }

    shark::core::log_message(
        shark::core::LogLevel::info,
        "sandbox",
        "Core diagnostics initialized");

    const auto engine_identity_matches =
        shark::core::engine_name() == "Shark";
    shark::core::shutdown_logging();

    return engine_identity_matches ? EXIT_SUCCESS : EXIT_FAILURE;
}
