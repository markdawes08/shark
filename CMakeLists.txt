cmake_minimum_required(VERSION 4.2)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are unsupported. Use: cmake --preset windows-vs2026")
endif()

project(Shark VERSION 0.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Shark currently supports Windows builds with MSVC only.")
endif()

if(NOT CMAKE_GENERATOR STREQUAL "Visual Studio 18 2026")
    message(FATAL_ERROR "Use the windows-vs2026 configure preset.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Shark requires an x64 build.")
endif()

if(NOT CMAKE_VS_PLATFORM_TOOLSET_VERSION MATCHES "^14\\.50(\\.|$)")
    message(FATAL_ERROR
        "Shark requires MSVC 14.50 LTS; selected ${CMAKE_VS_PLATFORM_TOOLSET_VERSION}.")
endif()

if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION OR
   NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_EQUAL "10.0.26100.0")
    message(FATAL_ERROR
        "Shark requires the pinned Windows SDK 10.0.26100.0; selected "
        "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING
    "Supported Shark build configurations" FORCE)
foreach(configuration IN ITEMS Debug Release)
    string(TOUPPER "${configuration}" configuration_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}
        "${CMAKE_BINARY_DIR}/bin/${configuration}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
        "${CMAKE_BINARY_DIR}/bin/${configuration}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}
        "${CMAKE_BINARY_DIR}/lib/${configuration}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CTest)
include(cmake/SharkCompilerOptions.cmake)

find_package(directx-headers CONFIG REQUIRED)

# The Agility vcpkg config locates the Windows SDK import library with a plain
# find_library call. Supply the exact SDK path selected by Shark because the
# Visual Studio generator does not add UM library directories to CMake's
# configure-time search path.
if(SHARK_UCRT_CONTENT_ROOT AND CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    file(TO_CMAKE_PATH "${SHARK_UCRT_CONTENT_ROOT}"
        _shark_windows_kits_root)
    string(CONCAT _shark_d3d12_import_library
        "${_shark_windows_kits_root}/Lib/"
        "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x64/d3d12.lib")
    cmake_path(NORMAL_PATH _shark_d3d12_import_library)
    if(NOT EXISTS "${_shark_d3d12_import_library}")
        message(FATAL_ERROR
            "The pinned Windows SDK D3D12 import library is missing: "
            "${_shark_d3d12_import_library}")
    endif()
    set(D3D12_LIB "${_shark_d3d12_import_library}" CACHE FILEPATH
        "Pinned Windows SDK D3D12 import library" FORCE)
    unset(_shark_d3d12_import_library)
    unset(_shark_windows_kits_root)
endif()

find_package(directx12-agility CONFIG REQUIRED)
find_package(directx-warp CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)

if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_HOST_TRIPLET)
    message(FATAL_ERROR
        "Pinned DXC validation requires the manifest install directory and "
        "host triplet.")
endif()

set(_shark_expected_dxc
    "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/directx-dxc/dxc.exe")
file(REAL_PATH "${_shark_expected_dxc}" _shark_expected_dxc_real)
file(REAL_PATH "${DIRECTX_DXC_TOOL}" _shark_resolved_dxc_real)
string(TOLOWER "${_shark_expected_dxc_real}" _shark_expected_dxc_compare)
string(TOLOWER "${_shark_resolved_dxc_real}" _shark_resolved_dxc_compare)
if(NOT EXISTS "${_shark_resolved_dxc_real}" OR
   NOT _shark_resolved_dxc_compare STREQUAL _shark_expected_dxc_compare)
    message(FATAL_ERROR
        "DXC must resolve from the active vcpkg host-tool install. Expected "
        "${_shark_expected_dxc_real}; resolved ${_shark_resolved_dxc_real}.")
endif()

get_filename_component(
    _shark_dxc_directory
    "${_shark_resolved_dxc_real}"
    DIRECTORY
)
set(_shark_dxc_runtime_files
    "${_shark_dxc_directory}/dxcompiler.dll"
    "${_shark_dxc_directory}/dxil.dll"
)
foreach(_shark_dxc_runtime IN LISTS _shark_dxc_runtime_files)
    if(NOT EXISTS "${_shark_dxc_runtime}")
        message(FATAL_ERROR
            "The pinned DXC host tool is incomplete: "
            "${_shark_dxc_runtime}")
    endif()
endforeach()

execute_process(
    COMMAND "${_shark_resolved_dxc_real}" --version
    RESULT_VARIABLE _shark_dxc_version_result
    OUTPUT_VARIABLE _shark_dxc_version_stdout
    ERROR_VARIABLE _shark_dxc_version_stderr
    ENCODING UTF-8
)
string(CONCAT _shark_dxc_version_text
    "${_shark_dxc_version_stdout}"
    "\n"
    "${_shark_dxc_version_stderr}"
)
set(_shark_required_dxc_version "1.9.2602.24")
string(REPLACE
    "."
    "[.]"
    _shark_required_dxc_version_pattern
    "${_shark_required_dxc_version}"
)
if(NOT _shark_dxc_version_result EQUAL 0 OR
   NOT _shark_dxc_version_text MATCHES
       "(^|[^0-9])${_shark_required_dxc_version_pattern}([^0-9]|$)")
    message(FATAL_ERROR
        "Shark requires retail DXC ${_shark_required_dxc_version}; "
        "reported: ${_shark_dxc_version_text}")
endif()

set(SHARK_DXC_TOOL
    "${_shark_resolved_dxc_real}"
    CACHE INTERNAL "Validated project-restored DXC host tool"
)
set(SHARK_DXC_RUNTIME_FILES
    "${_shark_dxc_runtime_files}"
    CACHE INTERNAL "Runtime files required beside the DXC host tool"
)
set(SHARK_DXC_VERSION
    "${_shark_required_dxc_version}"
    CACHE INTERNAL "Validated retail DXC version"
)

include(cmake/SharkShaders.cmake)

add_subdirectory(shaders)
add_subdirectory(engine)
add_subdirectory(apps/sandbox)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SharkSandbox)

message(STATUS "Shark generator: ${CMAKE_GENERATOR}")
message(STATUS "Shark MSVC toolset: ${CMAKE_VS_PLATFORM_TOOLSET_VERSION}")
message(STATUS "Shark Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
message(STATUS "Shark vcpkg root: ${SHARK_VCPKG_ROOT}")
message(STATUS "Shark DXC ${SHARK_DXC_VERSION}: ${SHARK_DXC_TOOL}")
