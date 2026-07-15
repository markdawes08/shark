find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(SharkTests
    unit/assertion_tests.cpp
    unit/engine_info_tests.cpp
    unit/logging_tests.cpp
    unit/result_tests.cpp
    unit/windows_application_tests.cpp
)

target_link_libraries(SharkTests
    PRIVATE
        Catch2::Catch2WithMain
        SharkEngine
        user32
)

target_compile_definitions(SharkTests
    PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
)

shark_configure_target(SharkTests)
set_property(TARGET SharkTests PROPERTY FOLDER "Shark")

catch_discover_tests(SharkTests
    TEST_PREFIX "unit."
    DISCOVERY_MODE PRE_TEST
    SKIP_IS_FAILURE
    PROPERTIES
        LABELS "unit"
        TIMEOUT 10
)

add_test(
    NAME integration.platform.window_shell
    COMMAND SharkSandbox --platform-smoke
)

set_tests_properties(integration.platform.window_shell
    PROPERTIES
        LABELS "integration;platform"
        RUN_SERIAL TRUE
        TIMEOUT 10
)
