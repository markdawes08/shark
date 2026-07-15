get_filename_component(_directx_warp_root
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE)
set(_directx_warp_debug
    "${_directx_warp_root}/debug/bin/d3d10warp.dll")
set(_directx_warp_release
    "${_directx_warp_root}/bin/d3d10warp.dll")

if(NOT EXISTS "${_directx_warp_debug}" OR
   NOT EXISTS "${_directx_warp_release}")
    message(FATAL_ERROR
        "The restored Microsoft.Direct3D.WARP runtime is incomplete under "
        "${_directx_warp_root}")
endif()

if(NOT TARGET Microsoft::DirectX-WARP)
    add_library(Microsoft::DirectX-WARP UNKNOWN IMPORTED)
    set_target_properties(Microsoft::DirectX-WARP PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG
            "${_directx_warp_debug}"
        IMPORTED_LOCATION_RELEASE
            "${_directx_warp_release}"
    )
endif()

set(directx-warp_FOUND TRUE)
unset(_directx_warp_debug)
unset(_directx_warp_release)
unset(_directx_warp_root)
