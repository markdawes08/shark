find_package(directxtex CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)

# The pinned shared spdlog target disables MSVC 4251/4275 in every consumer.
# Its headers are already imported as external, so keep Shark-owned warning
# state strict and rely on /external:W0 only for third-party headers.
set_property(TARGET spdlog::spdlog PROPERTY INTERFACE_COMPILE_OPTIONS "")

add_library(SharkEngine STATIC
    assets/include/shark/assets/dds_cubemap.hpp
    assets/include/shark/assets/environment_lighting.hpp
    assets/src/dds_cubemap.cpp
    assets/src/environment_lighting.cpp
    core/include/shark/core/assertion.hpp
    core/include/shark/core/error.hpp
    core/include/shark/core/engine_info.hpp
    core/include/shark/core/logging.hpp
    core/include/shark/core/math.hpp
    core/include/shark/core/result.hpp
    core/src/assertion.cpp
    core/src/error.cpp
    core/src/engine_info.cpp
    core/src/logging.cpp
    core/src/math.cpp
    physics/include/shark/physics/ballistic_body.hpp
    physics/include/shark/physics/box_collider.hpp
    physics/include/shark/physics/box_contact.hpp
    physics/include/shark/physics/capsule_collider.hpp
    physics/include/shark/physics/capsule_contact.hpp
    physics/include/shark/physics/contact_constraint.hpp
    physics/include/shark/physics/rigid_body.hpp
    physics/include/shark/physics/sphere_body_collision.hpp
    physics/include/shark/physics/sphere_collider.hpp
    physics/include/shark/physics/sphere_terrain_contact.hpp
    physics/src/ballistic_body.cpp
    physics/src/box_contact.cpp
    physics/src/capsule_contact.cpp
    physics/src/contact_constraint.cpp
    physics/src/rigid_body.cpp
    physics/src/sphere_body_collision.cpp
    physics/src/sphere_terrain_contact.cpp
    platform/windows/include/shark/platform/application.hpp
    platform/windows/include/shark/platform/events.hpp
    platform/windows/src/application.cpp
    render_graph/include/shark/render_graph/render_graph.hpp
    render_graph/src/render_graph.cpp
    renderer/include/shark/renderer/renderer.hpp
    renderer/src/d3d12/cube_scene_data.hpp
    renderer/src/d3d12/daylight_scene_data.hpp
    renderer/src/d3d12/environment_scene_data.hpp
    renderer/src/d3d12/gpu_timestamp_state.cpp
    renderer/src/d3d12/gpu_timestamp_state.hpp
    renderer/src/d3d12/renderer.cpp
    renderer/src/d3d12/skybox_scene_data.hpp
    renderer/src/d3d12/terrain_scene_data.hpp
    renderer/src/d3d12/water_scene_data.hpp
    renderer/src/frame_pipeline.cpp
    renderer/src/frame_pipeline.hpp
    renderer/src/terrain_frustum_culling.cpp
    renderer/src/terrain_frustum_culling.hpp
    renderer/src/terrain_lod_selection.cpp
    renderer/src/terrain_lod_selection.hpp
    rhi/d3d12/include/shark/rhi/d3d12/device.hpp
    rhi/d3d12/src/device.cpp
    rhi/d3d12/src/device_access.hpp
    rhi/d3d12/src/frame_resource_state.cpp
    rhi/d3d12/src/frame_resource_state.hpp
    rhi/d3d12/src/render_graph_executor.cpp
    rhi/d3d12/src/render_graph_executor.hpp
    simulation/include/shark/simulation/fixed_step_clock.hpp
    simulation/src/fixed_step_clock.cpp
    terrain/include/shark/terrain/height_tile.hpp
    terrain/include/shark/terrain/lake_basin.hpp
    terrain/include/shark/terrain/material_palette.hpp
    terrain/src/height_tile.cpp
    terrain/src/lake_basin.cpp
    terrain/src/material_palette.cpp
    world/include/shark/world/camera.hpp
    world/include/shark/world/environment_lab_scenario.hpp
    world/src/camera.cpp
    world/src/environment_lab_scenario.cpp
)

target_include_directories(SharkEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/render_graph/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/d3d12/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/simulation/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/terrain/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/world/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/d3d12/src"
)

target_compile_definitions(SharkEngine
    PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
        USE_PIX_RETAIL
)

target_link_libraries(SharkEngine
    PRIVATE
        spdlog::spdlog
        Microsoft::DirectXTex
        Microsoft::DirectX-Headers
        Microsoft::DirectX-Guids
        Microsoft::DirectXMath
        Microsoft::DirectX12-Agility
        Microsoft::WinPixEventRuntime
        dxgi
        dxguid
        user32
)

shark_configure_target(SharkEngine)
set_property(TARGET SharkEngine PROPERTY FOLDER "Shark")
