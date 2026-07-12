find_package(Catch2 3 CONFIG REQUIRED)

add_executable(SharkTests
    unit/engine_info_tests.cpp
)

target_link_libraries(SharkTests
    PRIVATE
        Catch2::Catch2WithMain
        SharkEngine
)

shark_configure_target(SharkTests)
set_property(TARGET SharkTests PROPERTY FOLDER "Shark")

add_test(NAME unit.engine_identity COMMAND SharkTests)

