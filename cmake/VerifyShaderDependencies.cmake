foreach(required_variable IN ITEMS
    SHARK_BUILD_CONFIG
    SHARK_BUILD_DIRECTORY
    SHARK_DEPENDENCY_PROBE_HEADER
    SHARK_DEPENDENCY_PROBE_INCLUDE
    SHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE
    SHARK_DEPENDENCY_PROBE_TARGET
    SHARK_SHADER_TARGET
    SHARK_PRIMARY_SHADER_NAME
    SHARK_SHARED_SHADER_NAME
    SHARK_VERTEX_DEPFILE
    SHARK_PIXEL_DEPFILE
)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Shader dependency verification is missing ${required_variable}.")
    endif()
endforeach()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${SHARK_BUILD_DIRECTORY}"
        --config "${SHARK_BUILD_CONFIG}"
        --target "${SHARK_SHADER_TARGET}"
    RESULT_VARIABLE shader_build_result
    OUTPUT_VARIABLE shader_build_stdout
    ERROR_VARIABLE shader_build_stderr
    ENCODING UTF-8
)
if(NOT shader_build_result EQUAL 0)
    message(FATAL_ERROR
        "The shader dependency target failed before depfiles could be "
        "checked:\n${shader_build_stdout}\n${shader_build_stderr}")
endif()

foreach(depfile IN ITEMS
    "${SHARK_VERTEX_DEPFILE}"
    "${SHARK_PIXEL_DEPFILE}"
)
    if(NOT EXISTS "${depfile}")
        message(FATAL_ERROR "Shader depfile does not exist: ${depfile}")
    endif()
    file(READ "${depfile}" depfile_contents)
    if(NOT depfile_contents MATCHES "${SHARK_PRIMARY_SHADER_NAME}")
        message(FATAL_ERROR
            "${depfile} does not track ${SHARK_PRIMARY_SHADER_NAME}.")
    endif()
    if(NOT depfile_contents MATCHES "${SHARK_SHARED_SHADER_NAME}")
        message(FATAL_ERROR
            "${depfile} does not track ${SHARK_SHARED_SHADER_NAME}.")
    endif()
endforeach()

file(COPY_FILE
    "${SHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE}"
    "${SHARK_DEPENDENCY_PROBE_INCLUDE}"
    ONLY_IF_DIFFERENT
)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${SHARK_BUILD_DIRECTORY}"
        --config "${SHARK_BUILD_CONFIG}"
        --target "${SHARK_DEPENDENCY_PROBE_TARGET}"
    RESULT_VARIABLE initial_probe_result
    OUTPUT_VARIABLE initial_probe_stdout
    ERROR_VARIABLE initial_probe_stderr
    ENCODING UTF-8
)
if(NOT initial_probe_result EQUAL 0 OR
   NOT EXISTS "${SHARK_DEPENDENCY_PROBE_HEADER}")
    message(FATAL_ERROR
        "The initial shader dependency probe failed:\n"
        "${initial_probe_stdout}\n${initial_probe_stderr}")
endif()
file(SHA256 "${SHARK_DEPENDENCY_PROBE_HEADER}" initial_probe_hash)

file(WRITE
    "${SHARK_DEPENDENCY_PROBE_INCLUDE}"
    "static const float dependency_probe_red = 0.75F;\n"
)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${SHARK_BUILD_DIRECTORY}"
        --config "${SHARK_BUILD_CONFIG}"
        --target "${SHARK_DEPENDENCY_PROBE_TARGET}"
    RESULT_VARIABLE changed_probe_result
    OUTPUT_VARIABLE changed_probe_stdout
    ERROR_VARIABLE changed_probe_stderr
    ENCODING UTF-8
)
if(EXISTS "${SHARK_DEPENDENCY_PROBE_HEADER}")
    file(SHA256 "${SHARK_DEPENDENCY_PROBE_HEADER}" changed_probe_hash)
else()
    set(changed_probe_hash "")
endif()

file(COPY_FILE
    "${SHARK_DEPENDENCY_PROBE_INCLUDE_FIXTURE}"
    "${SHARK_DEPENDENCY_PROBE_INCLUDE}"
    ONLY_IF_DIFFERENT
)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${SHARK_BUILD_DIRECTORY}"
        --config "${SHARK_BUILD_CONFIG}"
        --target "${SHARK_DEPENDENCY_PROBE_TARGET}"
    RESULT_VARIABLE restored_probe_result
    OUTPUT_VARIABLE restored_probe_stdout
    ERROR_VARIABLE restored_probe_stderr
    ENCODING UTF-8
)
if(NOT restored_probe_result EQUAL 0)
    message(FATAL_ERROR
        "The shader dependency probe could not restore its build-tree "
        "fixture:\n${restored_probe_stdout}\n${restored_probe_stderr}")
endif()
if(NOT changed_probe_result EQUAL 0)
    message(FATAL_ERROR
        "Rebuilding after the included HLSL changed failed:\n"
        "${changed_probe_stdout}\n${changed_probe_stderr}")
endif()
if(changed_probe_hash STREQUAL initial_probe_hash)
    message(FATAL_ERROR
        "Changing an included HLSL file did not regenerate the compiled "
        "shader header. The build is not consuming DXC depfiles.")
endif()

message(STATUS
    "DXC depfiles track includes and an include edit rebuilds the shader.")
