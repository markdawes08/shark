add_executable(SharkSandbox
    "${PROJECT_SOURCE_DIR}/cmake/Shark.Windows.manifest"
    "${SHARK_CUBE_PIXEL_HEADER}"
    "${SHARK_CUBE_VERTEX_HEADER}"
    "${SHARK_SKYBOX_PIXEL_HEADER}"
    "${SHARK_SKYBOX_VERTEX_HEADER}"
    "${SHARK_TERRAIN_PIXEL_HEADER}"
    "${SHARK_TERRAIN_VERTEX_HEADER}"
    "${SHARK_MATERIAL_SPHERE_PIXEL_HEADER}"
    "${SHARK_MATERIAL_SPHERE_VERTEX_HEADER}"
    "${SHARK_TONE_MAP_PIXEL_HEADER}"
    "${SHARK_TONE_MAP_VERTEX_HEADER}"
    agility_sdk_exports.cpp
    camera_controller.cpp
    camera_controller.hpp
    main.cpp
    options.cpp
    options.hpp
)

add_dependencies(
    SharkSandbox
    "${SHARK_CUBE_SHADER_TARGET}"
    "${SHARK_SKYBOX_SHADER_TARGET}"
    "${SHARK_TERRAIN_SHADER_TARGET}"
    "${SHARK_MATERIAL_SPHERE_SHADER_TARGET}"
    "${SHARK_TONE_MAP_SHADER_TARGET}"
)

target_include_directories(SharkSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SHARK_CUBE_SHADER_INCLUDE_DIRECTORY}"
        "${SHARK_SKYBOX_SHADER_INCLUDE_DIRECTORY}"
        "${SHARK_TERRAIN_SHADER_INCLUDE_DIRECTORY}"
        "${SHARK_MATERIAL_SPHERE_SHADER_INCLUDE_DIRECTORY}"
        "${SHARK_TONE_MAP_SHADER_INCLUDE_DIRECTORY}"
)

target_link_libraries(SharkSandbox
    PRIVATE
        SharkEngine
        Microsoft::DirectX-Headers
)

target_compile_definitions(SharkSandbox
    PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
)

add_custom_command(TARGET SharkSandbox POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:SharkSandbox>/D3D12"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:SharkSandbox>/content/sky"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX12-Core>"
        "$<TARGET_FILE_DIR:SharkSandbox>/D3D12/D3D12Core.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX12-Layers>"
        "$<TARGET_FILE_DIR:SharkSandbox>/D3D12/d3d12SDKLayers.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX-WARP>"
        "$<TARGET_FILE_DIR:SharkSandbox>/d3d10warp.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_SOURCE_DIR}/content/source/sky/shark_orientation_sky_srgb.dds"
        "$<TARGET_FILE_DIR:SharkSandbox>/content/sky/shark_orientation_sky_srgb.dds"
    VERBATIM
)

shark_configure_target(SharkSandbox)
set_property(TARGET SharkSandbox PROPERTY FOLDER "Shark")
