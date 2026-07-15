find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(SharkTests
    unit/assertion_tests.cpp
    unit/engine_info_tests.cpp
    unit/logging_tests.cpp
    unit/result_tests.cpp
)

target_link_libraries(SharkTests
    PRIVATE
        Catch2::Catch2WithMain
        SharkEngine
)

shark_configure_target(SharkTests)
set_property(TARGET SharkTests PROPERTY FOLDER "Shark")

catch_discover_tests(SharkTests
    TEST_PREFIX "unit."
    DISCOVERY_MODE PRE_TEST
    SKIP_IS_FAILURE
    PROPERTIES
        LABELS "unit"
)
