add_library(SharkEngine STATIC
    core/include/shark/core/engine_info.hpp
    core/src/engine_info.cpp
)

target_include_directories(SharkEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
)

shark_configure_target(SharkEngine)
set_property(TARGET SharkEngine PROPERTY FOLDER "Shark")

