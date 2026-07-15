set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(ARCHIVE
    URLS
        "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.warp/${VERSION}/microsoft.direct3d.warp.${VERSION}.nupkg"
    FILENAME "microsoft.direct3d.warp.${VERSION}.nupkg"
    SHA512
        b050e7bfd66588e5f256b1c4b882abc45fba8e1b05e3c83d5012c2763a83bef2efb60651b6363ade0cfb7dd0281939867290b89356714caa99dd08ce35682259
)

vcpkg_extract_source_archive(
    PACKAGE_PATH
    ARCHIVE "${ARCHIVE}"
    NO_REMOVE_ONE_LEVEL
)

foreach(destination IN ITEMS bin debug/bin)
    file(INSTALL
        "${PACKAGE_PATH}/build/native/bin/x64/d3d10warp.dll"
        "${PACKAGE_PATH}/build/native/bin/x64/d3d10warp.pdb"
        DESTINATION "${CURRENT_PACKAGES_DIR}/${destination}"
    )
endforeach()

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/directx-warp-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

vcpkg_install_copyright(FILE_LIST "${PACKAGE_PATH}/LICENSE.TXT")
