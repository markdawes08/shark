# Honor a caller-provided vcpkg first. Otherwise locate the stable Visual Studio
# 2026 instance that owns the bundled vcpkg component.
set(_shark_vcpkg_root "")

# Some Visual Studio 2026 installations register a partial 64-bit Windows Kits
# root before the complete x86 SDK root. MSBuild's ucrt.props prefers that first
# registry value, which makes ucrtd.lib invisible. Correct only this CMake
# process when the verified complete SDK is in the conventional x86 location.
if(CMAKE_HOST_WIN32 AND DEFINED ENV{SystemDrive})
    set(_shark_windows_kits_root
        "$ENV{SystemDrive}/Program Files (x86)/Windows Kits/10")
    if(EXISTS "${_shark_windows_kits_root}/Lib/10.0.26100.0/ucrt/x64/ucrtd.lib")
        file(TO_NATIVE_PATH "${_shark_windows_kits_root}/"
            _shark_windows_kits_root_native)
        set(ENV{UCRTContentRoot} "${_shark_windows_kits_root_native}")
        set(SHARK_UCRT_CONTENT_ROOT "${_shark_windows_kits_root_native}"
            CACHE INTERNAL "Complete Windows SDK root used by MSBuild")
    endif()
endif()

if(DEFINED ENV{VCPKG_ROOT} AND
   EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _shark_vcpkg_root)
endif()

if(_shark_vcpkg_root STREQUAL "")
    find_program(_shark_vcpkg_executable NAMES vcpkg.exe NO_CACHE)
    if(_shark_vcpkg_executable)
        get_filename_component(_shark_path_vcpkg_root
            "${_shark_vcpkg_executable}" DIRECTORY)
        if(EXISTS
           "${_shark_path_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            file(TO_CMAKE_PATH "${_shark_path_vcpkg_root}"
                _shark_vcpkg_root)
        endif()
    endif()
endif()

if(_shark_vcpkg_root STREQUAL "")
    if(NOT CMAKE_HOST_WIN32)
        message(FATAL_ERROR
            "Shark requires vcpkg. Set VCPKG_ROOT to a compatible installation.")
    endif()

    set(_shark_program_files_x86 "")
    if(DEFINED ENV{ProgramW6432})
        get_filename_component(_shark_system_drive "$ENV{ProgramW6432}" DIRECTORY)
        set(_shark_program_files_x86 "${_shark_system_drive}/Program Files (x86)")
    elseif(DEFINED ENV{SystemDrive})
        set(_shark_program_files_x86 "$ENV{SystemDrive}/Program Files (x86)")
    endif()

    find_program(_shark_vswhere
        NAMES vswhere.exe
        HINTS "${_shark_program_files_x86}/Microsoft Visual Studio/Installer"
        NO_DEFAULT_PATH)

    if(NOT _shark_vswhere)
        message(FATAL_ERROR
            "vswhere.exe was not found. Install Visual Studio 2026 or set VCPKG_ROOT.")
    endif()

    execute_process(
        COMMAND "${_shark_vswhere}"
            -latest
            -products *
            -version "[18.0,19.0)"
            -requires
                Microsoft.VisualStudio.Component.Vcpkg
                Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64
            -property installationPath
            -utf8
        RESULT_VARIABLE _shark_vswhere_result
        OUTPUT_VARIABLE _shark_visual_studio_root
        ERROR_VARIABLE _shark_vswhere_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ENCODING UTF-8)

    if(NOT _shark_vswhere_result EQUAL 0 OR
       _shark_visual_studio_root STREQUAL "")
        message(FATAL_ERROR
            "No stable Visual Studio 2026 vcpkg component was found. "
            "Install the vcpkg individual component or set VCPKG_ROOT. "
            "vswhere: ${_shark_vswhere_error}")
    endif()

    file(TO_CMAKE_PATH
        "${_shark_visual_studio_root}/VC/vcpkg"
        _shark_vcpkg_root)
endif()

set(_shark_vcpkg_toolchain
    "${_shark_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${_shark_vcpkg_toolchain}")
    message(FATAL_ERROR
        "The selected vcpkg root is incomplete: ${_shark_vcpkg_root}")
endif()

set(ENV{VCPKG_ROOT} "${_shark_vcpkg_root}")
set(VCPKG_ROOT "${_shark_vcpkg_root}" CACHE PATH
    "vcpkg root selected for Shark")
set(SHARK_VCPKG_ROOT "${_shark_vcpkg_root}" CACHE INTERNAL
    "vcpkg root selected for Shark")

include("${_shark_vcpkg_toolchain}")

unset(_shark_program_files_x86)
unset(_shark_path_vcpkg_root)
unset(_shark_system_drive)
unset(_shark_vcpkg_executable)
unset(_shark_vcpkg_root)
unset(_shark_vcpkg_toolchain)
unset(_shark_visual_studio_root)
unset(_shark_vswhere)
unset(_shark_vswhere_error)
unset(_shark_vswhere_result)
unset(_shark_windows_kits_root)
unset(_shark_windows_kits_root_native)
