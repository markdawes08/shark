find_package(spdlog CONFIG REQUIRED)

# The pinned shared spdlog target disables MSVC 4251/4275 in every consumer.
# Its headers are already imported as external, so keep Shark-owned warning
# state strict and rely on /external:W0 only for third-party headers.
set_property(TARGET spdlog::spdlog PROPERTY INTERFACE_COMPILE_OPTIONS "")

add_library(SharkEngine STATIC
    core/include/shark/core/assertion.hpp
    core/include/shark/core/error.hpp
    core/include/shark/core/engine_info.hpp
    core/include/shark/core/logging.hpp
    core/include/shark/core/result.hpp
    core/src/assertion.cpp
    core/src/error.cpp
    core/src/engine_info.cpp
    core/src/logging.cpp
    platform/windows/include/shark/platform/application.hpp
    platform/windows/include/shark/platform/events.hpp
    platform/windows/src/application.cpp
)

target_include_directories(SharkEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/include"
)

target_compile_definitions(SharkEngine
    PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
)

target_link_libraries(SharkEngine
    PRIVATE
        spdlog::spdlog
        user32
)

shark_configure_target(SharkEngine)
set_property(TARGET SharkEngine PROPERTY FOLDER "Shark")
