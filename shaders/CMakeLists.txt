set(cube_shader_source
    "${CMAKE_CURRENT_SOURCE_DIR}/cube/cube.hlsl")
set(cube_shader_include
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/camera_constants.hlsli")

shark_compile_hlsl(
    SOURCE "${cube_shader_source}"
    ENTRY_POINT VSMain
    PROFILE vs_6_0
    OUTPUT_NAME cube.vertex
    SYMBOL shark_cube_vertex_shader
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)
set(cube_vertex_binary "${SHARK_SHADER_BINARY}")
set(cube_vertex_header "${SHARK_SHADER_HEADER}")
set(cube_vertex_pdb "${SHARK_SHADER_PDB}")
set(cube_vertex_depfile "${SHARK_SHADER_DEPFILE}")

shark_compile_hlsl(
    SOURCE "${cube_shader_source}"
    ENTRY_POINT PSMain
    PROFILE ps_6_0
    OUTPUT_NAME cube.pixel
    SYMBOL shark_cube_pixel_shader
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)
set(cube_pixel_binary "${SHARK_SHADER_BINARY}")
set(cube_pixel_header "${SHARK_SHADER_HEADER}")
set(cube_pixel_pdb "${SHARK_SHADER_PDB}")
set(cube_pixel_depfile "${SHARK_SHADER_DEPFILE}")

add_custom_target(SharkCubeShaders
    DEPENDS
        "${cube_vertex_binary}"
        "${cube_vertex_depfile}"
        "${cube_vertex_header}"
        "${cube_vertex_pdb}"
        "${cube_pixel_binary}"
        "${cube_pixel_depfile}"
        "${cube_pixel_header}"
        "${cube_pixel_pdb}"
    SOURCES
        "${cube_shader_source}"
        "${cube_shader_include}"
)
set_property(TARGET SharkCubeShaders PROPERTY FOLDER "Shark/Shaders")

set(SHARK_CUBE_SHADER_TARGET
    SharkCubeShaders
    PARENT_SCOPE
)
set(SHARK_CUBE_SHADER_INCLUDE_DIRECTORY
    "${CMAKE_BINARY_DIR}/generated/shaders/$<CONFIG>"
    PARENT_SCOPE
)
set(SHARK_CUBE_VERTEX_HEADER
    "${cube_vertex_header}"
    PARENT_SCOPE
)
set(SHARK_CUBE_PIXEL_HEADER
    "${cube_pixel_header}"
    PARENT_SCOPE
)
set(SHARK_CUBE_VERTEX_DEPFILE
    "${cube_vertex_depfile}"
    PARENT_SCOPE
)
set(SHARK_CUBE_PIXEL_DEPFILE
    "${cube_pixel_depfile}"
    PARENT_SCOPE
)

if(BUILD_TESTING)
    set(dependency_probe_source_directory
        "${CMAKE_BINARY_DIR}/generated/shader-dependency-probe/source")
    file(MAKE_DIRECTORY "${dependency_probe_source_directory}")
    set(dependency_probe_source_fixture
        "${PROJECT_SOURCE_DIR}/tests/fixtures/shaders/dependency_probe.hlsl")
    set(dependency_probe_include_fixture
        "${PROJECT_SOURCE_DIR}/tests/fixtures/shaders/dependency_probe_values.hlsli")
    set(dependency_probe_source
        "${dependency_probe_source_directory}/dependency_probe.hlsl")
    set(dependency_probe_include
        "${dependency_probe_source_directory}/dependency_probe_values.hlsli")
    configure_file(
        "${dependency_probe_source_fixture}"
        "${dependency_probe_source}"
        COPYONLY
    )
    configure_file(
        "${dependency_probe_include_fixture}"
        "${dependency_probe_include}"
        COPYONLY
    )
    shark_compile_hlsl(
        SOURCE "${dependency_probe_source}"
        ENTRY_POINT PSMain
        PROFILE ps_6_0
        OUTPUT_NAME verification/dependency_probe
        SYMBOL shark_dependency_probe_shader
        INCLUDE_DIRECTORIES "${dependency_probe_source_directory}"
    )
    add_custom_target(SharkShaderDependencyProbe
        DEPENDS
            "${SHARK_SHADER_BINARY}"
            "${SHARK_SHADER_DEPFILE}"
            "${SHARK_SHADER_HEADER}"
            "${SHARK_SHADER_PDB}"
        SOURCES
            "${dependency_probe_source}"
            "${dependency_probe_include}"
    )
    set_property(
        TARGET SharkShaderDependencyProbe
        PROPERTY FOLDER "Shark/Verification"
    )
    set(SHARK_DEPENDENCY_PROBE_TARGET
        SharkShaderDependencyProbe
        PARENT_SCOPE
    )
    set(SHARK_DEPENDENCY_PROBE_HEADER
        "${SHARK_SHADER_HEADER}"
        PARENT_SCOPE
    )
    set(SHARK_DEPENDENCY_PROBE_INCLUDE
        "${dependency_probe_include}"
        PARENT_SCOPE
    )
    set(SHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE
        "${dependency_probe_include_fixture}"
        PARENT_SCOPE
    )

    set(malformed_shader_source
        "${PROJECT_SOURCE_DIR}/tests/fixtures/shaders/malformed.hlsl")
    shark_compile_hlsl(
        SOURCE "${malformed_shader_source}"
        ENTRY_POINT VSMain
        PROFILE vs_6_0
        OUTPUT_NAME negative/malformed
        SYMBOL shark_malformed_shader
    )
    add_custom_target(SharkMalformedShaderProbe
        DEPENDS
            "${SHARK_SHADER_BINARY}"
            "${SHARK_SHADER_HEADER}"
        SOURCES "${malformed_shader_source}"
    )
    set_property(
        TARGET SharkMalformedShaderProbe
        PROPERTY FOLDER "Shark/Verification"
    )
    set(SHARK_MALFORMED_SHADER_HEADER
        "${SHARK_SHADER_HEADER}"
        PARENT_SCOPE
    )
    set(SHARK_MALFORMED_SHADER_BINARY
        "${SHARK_SHADER_BINARY}"
        PARENT_SCOPE
    )
    set(SHARK_MALFORMED_SHADER_PDB
        "${SHARK_SHADER_PDB}"
        PARENT_SCOPE
    )
    set(SHARK_MALFORMED_SHADER_DEPFILE
        "${SHARK_SHADER_DEPFILE}"
        PARENT_SCOPE
    )

    set(warning_shader_source
        "${PROJECT_SOURCE_DIR}/tests/fixtures/shaders/warning.hlsl")
    shark_compile_hlsl(
        SOURCE "${warning_shader_source}"
        ENTRY_POINT PSMain
        PROFILE ps_6_0
        OUTPUT_NAME negative/warning
        SYMBOL shark_warning_shader
    )
    add_custom_target(SharkWarningShaderProbe
        DEPENDS
            "${SHARK_SHADER_BINARY}"
            "${SHARK_SHADER_HEADER}"
        SOURCES "${warning_shader_source}"
    )
    set_property(
        TARGET SharkWarningShaderProbe
        PROPERTY FOLDER "Shark/Verification"
    )
    set(SHARK_WARNING_SHADER_HEADER
        "${SHARK_SHADER_HEADER}"
        PARENT_SCOPE
    )
    set(SHARK_WARNING_SHADER_BINARY
        "${SHARK_SHADER_BINARY}"
        PARENT_SCOPE
    )
    set(SHARK_WARNING_SHADER_PDB
        "${SHARK_SHADER_PDB}"
        PARENT_SCOPE
    )
    set(SHARK_WARNING_SHADER_DEPFILE
        "${SHARK_SHADER_DEPFILE}"
        PARENT_SCOPE
    )
endif()
