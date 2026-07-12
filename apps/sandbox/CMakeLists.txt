add_executable(SharkSandbox
    main.cpp
)

target_link_libraries(SharkSandbox
    PRIVATE
        SharkEngine
)

shark_configure_target(SharkSandbox)
set_property(TARGET SharkSandbox PROPERTY FOLDER "Shark")
