foreach(required_variable IN ITEMS
    SHARK_BUILD_CONFIG
    SHARK_BUILD_DIRECTORY
    SHARK_DXC_TOOL
    SHARK_EXPECTED_DXC_VERSION
    SHARK_EXPECTED_SOURCE_NAME
    SHARK_EXPECTED_SENTINEL
    SHARK_PROBE_HEADER
    SHARK_PROBE_BINARY
    SHARK_PROBE_PDB
    SHARK_PROBE_DEPFILE
    SHARK_PROBE_TARGET
)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Expected-build-failure check is missing ${required_variable}.")
    endif()
endforeach()

execute_process(
    COMMAND "${SHARK_DXC_TOOL}" --version
    RESULT_VARIABLE dxc_result
    OUTPUT_VARIABLE dxc_stdout
    ERROR_VARIABLE dxc_stderr
    ENCODING UTF-8
)
string(CONCAT dxc_version_text "${dxc_stdout}" "\n" "${dxc_stderr}")
string(REPLACE
    "."
    "[.]"
    expected_dxc_version_pattern
    "${SHARK_EXPECTED_DXC_VERSION}"
)
if(NOT dxc_result EQUAL 0 OR
   NOT dxc_version_text MATCHES
       "(^|[^0-9])${expected_dxc_version_pattern}([^0-9]|$)")
    message(FATAL_ERROR
        "The expected-failure check could not verify pinned DXC "
        "${SHARK_EXPECTED_DXC_VERSION}: ${dxc_version_text}")
endif()

file(REMOVE
    "${SHARK_PROBE_HEADER}"
    "${SHARK_PROBE_BINARY}"
    "${SHARK_PROBE_PDB}"
    "${SHARK_PROBE_DEPFILE}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${SHARK_BUILD_DIRECTORY}"
        --config "${SHARK_BUILD_CONFIG}"
        --target "${SHARK_PROBE_TARGET}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
    ENCODING UTF-8
)
string(CONCAT build_diagnostics "${build_stdout}" "\n" "${build_stderr}")

if(build_result EQUAL 0)
    message(FATAL_ERROR
        "${SHARK_PROBE_TARGET} unexpectedly compiled successfully.")
endif()
if(NOT build_diagnostics MATCHES "${SHARK_EXPECTED_SOURCE_NAME}")
    message(FATAL_ERROR
        "${SHARK_PROBE_TARGET} failed without naming the expected source "
        "${SHARK_EXPECTED_SOURCE_NAME}:\n${build_diagnostics}")
endif()
if(NOT build_diagnostics MATCHES "${SHARK_EXPECTED_SENTINEL}")
    message(FATAL_ERROR
        "${SHARK_PROBE_TARGET} failed without the expected diagnostic "
        "${SHARK_EXPECTED_SENTINEL}:\n${build_diagnostics}")
endif()
if(NOT build_diagnostics MATCHES "[Ee]rror")
    message(FATAL_ERROR
        "${SHARK_PROBE_TARGET} failed without a compiler error diagnostic:\n"
        "${build_diagnostics}")
endif()

foreach(unexpected_output IN ITEMS
    SHARK_PROBE_HEADER
    SHARK_PROBE_BINARY
)
    if(EXISTS "${${unexpected_output}}")
        message(FATAL_ERROR
            "${SHARK_PROBE_TARGET} left a valid-looking output after DXC "
            "rejected the source: ${${unexpected_output}}")
    endif()
endforeach()

message(STATUS
    "${SHARK_PROBE_TARGET} was rejected by pinned DXC as expected.")
