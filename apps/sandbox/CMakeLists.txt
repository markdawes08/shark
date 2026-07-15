add_executable(SharkSandbox
    "${PROJECT_SOURCE_DIR}/cmake/Shark.Windows.manifest"
    agility_sdk_exports.cpp
    main.cpp
    options.cpp
    options.hpp
)

target_include_directories(SharkSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX12-Core>"
        "$<TARGET_FILE_DIR:SharkSandbox>/D3D12/D3D12Core.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX12-Layers>"
        "$<TARGET_FILE_DIR:SharkSandbox>/D3D12/d3d12SDKLayers.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Microsoft::DirectX-WARP>"
        "$<TARGET_FILE_DIR:SharkSandbox>/d3d10warp.dll"
    VERBATIM
)

shark_configure_target(SharkSandbox)
set_property(TARGET SharkSandbox PROPERTY FOLDER "Shark")
