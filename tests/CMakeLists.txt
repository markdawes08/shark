find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(SharkTests
    "${PROJECT_SOURCE_DIR}/cmake/Shark.Windows.manifest"
    ../apps/sandbox/camera_controller.cpp
    ../apps/sandbox/camera_controller.hpp
    ../apps/sandbox/options.cpp
    unit/camera_controller_tests.cpp
    unit/camera_tests.cpp
    unit/assertion_tests.cpp
    unit/dds_cubemap_tests.cpp
    unit/environment_lighting_tests.cpp
    unit/d3d12_device_contract_tests.cpp
    unit/d3d12_frame_resource_tests.cpp
    unit/d3d12_render_graph_executor_tests.cpp
    unit/engine_info_tests.cpp
    unit/logging_tests.cpp
    unit/math_tests.cpp
    unit/render_graph_tests.cpp
    unit/renderer_contract_tests.cpp
    unit/renderer_d3d12_cube_scene_data_tests.cpp
    unit/renderer_d3d12_daylight_scene_data_tests.cpp
    unit/renderer_d3d12_environment_scene_data_tests.cpp
    unit/renderer_d3d12_gpu_timestamp_tests.cpp
    unit/renderer_d3d12_skybox_scene_data_tests.cpp
    unit/renderer_d3d12_terrain_scene_data_tests.cpp
    unit/renderer_d3d12_water_scene_data_tests.cpp
    unit/renderer_frame_pipeline_tests.cpp
    unit/renderer_terrain_frustum_culling_tests.cpp
    unit/renderer_terrain_lod_selection_tests.cpp
    unit/result_tests.cpp
    unit/sandbox_options_tests.cpp
    unit/terrain_height_tile_tests.cpp
    unit/terrain_height_tile_query_tests.cpp
    unit/terrain_lake_basin_tests.cpp
    unit/terrain_material_palette_tests.cpp
    unit/environment_lab_scenario_tests.cpp
    unit/windows_application_tests.cpp
)

target_include_directories(SharkTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/sandbox"
        "${PROJECT_SOURCE_DIR}/engine/renderer/src"
        "${PROJECT_SOURCE_DIR}/engine/renderer/src/d3d12"
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
        SHARK_TEST_CUBEMAP_PATH=\"${PROJECT_SOURCE_DIR}/content/source/sky/shark_orientation_sky_srgb.dds\"
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
    NAME build.shader_dependencies
    COMMAND
        "${CMAKE_COMMAND}"
        "-DSHARK_BUILD_CONFIG=$<CONFIG>"
        "-DSHARK_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DSHARK_DEPENDENCY_PROBE_HEADER=${SHARK_DEPENDENCY_PROBE_HEADER}"
        "-DSHARK_DEPENDENCY_PROBE_INCLUDE=${SHARK_DEPENDENCY_PROBE_INCLUDE}"
        "-DSHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE=${SHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE}"
        "-DSHARK_DEPENDENCY_PROBE_TARGET=${SHARK_DEPENDENCY_PROBE_TARGET}"
        "-DSHARK_SHADER_TARGET=${SHARK_CUBE_SHADER_TARGET}"
        "-DSHARK_PRIMARY_SHADER_NAME=cube.hlsl"
        "-DSHARK_SHARED_SHADER_NAME=camera_constants.hlsli"
        "-DSHARK_VERTEX_DEPFILE=${SHARK_CUBE_VERTEX_DEPFILE}"
        "-DSHARK_PIXEL_DEPFILE=${SHARK_CUBE_PIXEL_DEPFILE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyShaderDependencies.cmake"
)

add_test(
    NAME build.shader_malformed_fails
    COMMAND
        "${CMAKE_COMMAND}"
        "-DSHARK_BUILD_CONFIG=$<CONFIG>"
        "-DSHARK_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DSHARK_DXC_TOOL=${SHARK_DXC_TOOL}"
        "-DSHARK_EXPECTED_DXC_VERSION=${SHARK_DXC_VERSION}"
        "-DSHARK_EXPECTED_SOURCE_NAME=malformed.hlsl"
        "-DSHARK_EXPECTED_SENTINEL=SHARK_EXPECTED_MALFORMED_SHADER_FAILURE"
        "-DSHARK_PROBE_HEADER=${SHARK_MALFORMED_SHADER_HEADER}"
        "-DSHARK_PROBE_BINARY=${SHARK_MALFORMED_SHADER_BINARY}"
        "-DSHARK_PROBE_PDB=${SHARK_MALFORMED_SHADER_PDB}"
        "-DSHARK_PROBE_DEPFILE=${SHARK_MALFORMED_SHADER_DEPFILE}"
        "-DSHARK_PROBE_TARGET=SharkMalformedShaderProbe"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyExpectedBuildFailure.cmake"
)

add_test(
    NAME build.shader_warning_is_error
    COMMAND
        "${CMAKE_COMMAND}"
        "-DSHARK_BUILD_CONFIG=$<CONFIG>"
        "-DSHARK_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DSHARK_DXC_TOOL=${SHARK_DXC_TOOL}"
        "-DSHARK_EXPECTED_DXC_VERSION=${SHARK_DXC_VERSION}"
        "-DSHARK_EXPECTED_SOURCE_NAME=warning.hlsl"
        "-DSHARK_EXPECTED_SENTINEL=SHARK_EXPECTED_WARNING_AS_ERROR"
        "-DSHARK_PROBE_HEADER=${SHARK_WARNING_SHADER_HEADER}"
        "-DSHARK_PROBE_BINARY=${SHARK_WARNING_SHADER_BINARY}"
        "-DSHARK_PROBE_PDB=${SHARK_WARNING_SHADER_PDB}"
        "-DSHARK_PROBE_DEPFILE=${SHARK_WARNING_SHADER_DEPFILE}"
        "-DSHARK_PROBE_TARGET=SharkWarningShaderProbe"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyExpectedBuildFailure.cmake"
)

set_tests_properties(
    build.shader_dependencies
    build.shader_malformed_fails
    build.shader_warning_is_error
    PROPERTIES
        LABELS "build;shader"
        RUN_SERIAL TRUE
        TIMEOUT 60
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
    NAME integration.gpu.hardware_cube_present
    COMMAND SharkSandbox --present-smoke
)

set_tests_properties(integration.gpu.hardware_cube_present
    PROPERTIES
        LABELS "integration;gpu;presentation;hardware"
        RUN_SERIAL TRUE
        TIMEOUT 60
)

add_test(
    NAME integration.gpu.warp_cube_present
    COMMAND SharkSandbox --present-smoke --warp
)

set_tests_properties(integration.gpu.warp_cube_present
    PROPERTIES
        LABELS "integration;gpu;presentation;warp"
        RUN_SERIAL TRUE
        TIMEOUT 150
)

add_test(
    NAME integration.gpu.warp_cube_present_validation
    COMMAND SharkSandbox --present-smoke --warp --gpu-validation
)

set_tests_properties(integration.gpu.warp_cube_present_validation
    PROPERTIES
        LABELS "integration;gpu;presentation;warp;validation"
        RUN_SERIAL TRUE
        TIMEOUT 240
)
