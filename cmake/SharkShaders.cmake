include_guard(GLOBAL)

include(CMakeParseArguments)

function(shark_compile_hlsl)
    set(options)
    set(one_value_arguments
        ENTRY_POINT
        OUTPUT_NAME
        PROFILE
        SOURCE
        SYMBOL
    )
    set(multi_value_arguments
        INCLUDE_DIRECTORIES
    )
    cmake_parse_arguments(
        SHADER
        "${options}"
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN}
    )

    foreach(required_argument IN ITEMS
        ENTRY_POINT
        OUTPUT_NAME
        PROFILE
        SOURCE
        SYMBOL
    )
        if(NOT SHADER_${required_argument})
            message(FATAL_ERROR
                "shark_compile_hlsl requires ${required_argument}.")
        endif()
    endforeach()

    if(NOT IS_ABSOLUTE "${SHADER_SOURCE}")
        cmake_path(
            ABSOLUTE_PATH SHADER_SOURCE
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            NORMALIZE
        )
    endif()
    if(NOT EXISTS "${SHADER_SOURCE}")
        message(FATAL_ERROR "HLSL source does not exist: ${SHADER_SOURCE}")
    endif()

    set(output_directory
        "${CMAKE_BINARY_DIR}/generated/shaders/$<CONFIG>")
    set(output_binary
        "${output_directory}/${SHADER_OUTPUT_NAME}.dxil")
    set(output_header
        "${output_directory}/${SHADER_OUTPUT_NAME}.hpp")
    set(output_pdb
        "${output_directory}/${SHADER_OUTPUT_NAME}.pdb")
    set(output_depfile
        "${output_directory}/${SHADER_OUTPUT_NAME}.d")
    get_filename_component(output_parent "${output_binary}" DIRECTORY)

    set(include_arguments)
    foreach(include_directory IN LISTS SHADER_INCLUDE_DIRECTORIES)
        if(NOT IS_ABSOLUTE "${include_directory}")
            cmake_path(
                ABSOLUTE_PATH include_directory
                BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                NORMALIZE
            )
        endif()
        list(APPEND include_arguments -I "${include_directory}")
    endforeach()

    add_custom_command(
        OUTPUT
            "${output_binary}"
            "${output_header}"
            "${output_pdb}"
            "${output_depfile}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_parent}"
        COMMAND "${SHARK_DXC_TOOL}"
            -nologo
            -E "${SHADER_ENTRY_POINT}"
            -T "${SHADER_PROFILE}"
            -HV 2021
            -Zpr
            -Ges
            -WX
            -encoding utf8
            -fdiagnostics-format=msvc
            -Zi
            -Qembed_debug
            -Qsource_in_debug_module
            "$<$<CONFIG:Debug>:-Od>"
            "$<$<CONFIG:Release>:-O3>"
            -Fo "${output_binary}"
            -Fh "${output_header}"
            -Vn "${SHADER_SYMBOL}"
            -Fd "${output_pdb}"
            ${include_arguments}
            "${SHADER_SOURCE}"
        COMMAND "${SHARK_DXC_TOOL}"
            -nologo
            -E "${SHADER_ENTRY_POINT}"
            -T "${SHADER_PROFILE}"
            -HV 2021
            -Zpr
            -Ges
            -WX
            -encoding utf8
            -fdiagnostics-format=msvc
            -Fo "${output_binary}"
            -MD
            -MF "${output_depfile}"
            ${include_arguments}
            "${SHADER_SOURCE}"
        MAIN_DEPENDENCY "${SHADER_SOURCE}"
        DEPENDS
            "${SHADER_SOURCE}"
            "${SHARK_DXC_TOOL}"
            ${SHARK_DXC_RUNTIME_FILES}
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        DEPFILE "${output_depfile}"
        COMMENT
            "Compiling ${SHADER_ENTRY_POINT} (${SHADER_PROFILE}) with pinned DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    set_source_files_properties(
        "${output_binary}"
        "${output_header}"
        "${output_pdb}"
        "${output_depfile}"
        PROPERTIES GENERATED TRUE
    )

    set(SHARK_SHADER_BINARY "${output_binary}" PARENT_SCOPE)
    set(SHARK_SHADER_HEADER "${output_header}" PARENT_SCOPE)
    set(SHARK_SHADER_PDB "${output_pdb}" PARENT_SCOPE)
    set(SHARK_SHADER_DEPFILE "${output_depfile}" PARENT_SCOPE)
endfunction()
