find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(SharkTests
    "${PROJECT_SOURCE_DIR}/cmake/Shark.Windows.manifest"
    ../apps/sandbox/options.cpp
    unit/assertion_tests.cpp
    unit/d3d12_device_contract_tests.cpp
    unit/d3d12_frame_resource_tests.cpp
    unit/engine_info_tests.cpp
    unit/logging_tests.cpp
    unit/result_tests.cpp
    unit/sandbox_options_tests.cpp
    unit/windows_application_tests.cpp
)

target_include_directories(SharkTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/sandbox"
        "${PROJECT_SOURCE_DIR}/engine/rhi/d3d12/src"
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

add_test(
    NAME integration.gpu.hardware_device
    COMMAND SharkSandbox --gpu-smoke
)

set_tests_properties(integration.gpu.hardware_device
    PROPERTIES
        LABELS "integration;gpu;hardware"
        RUN_SERIAL TRUE
        TIMEOUT 20
)

add_test(
    NAME integration.gpu.warp_device
    COMMAND SharkSandbox --gpu-smoke --warp
)

set_tests_properties(integration.gpu.warp_device
    PROPERTIES
        LABELS "integration;gpu;warp"
        RUN_SERIAL TRUE
        TIMEOUT 20
)

add_test(
    NAME integration.gpu.hardware_clear_present
    COMMAND SharkSandbox --present-smoke
)

set_tests_properties(integration.gpu.hardware_clear_present
    PROPERTIES
        LABELS "integration;gpu;presentation;hardware"
        RUN_SERIAL TRUE
        TIMEOUT 60
)

add_test(
    NAME integration.gpu.warp_clear_present
    COMMAND SharkSandbox --present-smoke --warp
)

set_tests_properties(integration.gpu.warp_clear_present
    PROPERTIES
        LABELS "integration;gpu;presentation;warp"
        RUN_SERIAL TRUE
        TIMEOUT 90
)

add_test(
    NAME integration.gpu.warp_clear_present_validation
    COMMAND SharkSandbox --present-smoke --warp --gpu-validation
)

set_tests_properties(integration.gpu.warp_clear_present_validation
    PROPERTIES
        LABELS "integration;gpu;presentation;warp;validation"
        RUN_SERIAL TRUE
        TIMEOUT 180
)
