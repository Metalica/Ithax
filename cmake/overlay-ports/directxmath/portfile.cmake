vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Microsoft/DirectXMath
    REF jun2026
    SHA512 47003d3c223e0b99f99346f0f6f971df1cab319dc975479b12a8b353022de603293761e1da78e811b824e649ec935de07b54c0abff308d4b0fcea05b7732e494
    HEAD_REF main
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        xdsp BUILD_XDSP
        dx11 BUILD_DX11
        dx12 BUILD_DX12
)

set(EXTRA_OPTIONS "")

if(("dx11" IN_LIST FEATURES) OR ("dx12" IN_LIST FEATURES))
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
    list(APPEND EXTRA_OPTIONS -DBUILD_SHMATH=ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS} ${EXTRA_OPTIONS}
    MAYBE_UNUSED_VARIABLES BUILD_DX11 BUILD_DX12
)

vcpkg_cmake_install()

# Fix: the original port fails on debug-only builds because DirectXMath.pc
# already exists in the destination from the release build.
# Use COPY instead of INSTALL to avoid "File exists" error.
set(_pc_src "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/DirectXMath.pc")
set(_pc_dst "${CURRENT_PACKAGES_DIR}/share/pkgconfig")
if(EXISTS "${_pc_src}" AND NOT EXISTS "${_pc_dst}/DirectXMath.pc")
    file(COPY "${_pc_src}" DESTINATION "${_pc_dst}")
endif()

vcpkg_fixup_pkgconfig()

# DirectXMath is header-only and its CMake doesn't install config files.
# Generate a minimal CMake config so find_package(DirectXMath CONFIG) works.
set(_config_dir "${CURRENT_PACKAGES_DIR}/share/directxmath")
file(WRITE "${_config_dir}/directxmathConfig.cmake"
"add_library(Microsoft::DirectXMath INTERFACE IMPORTED)
set_target_properties(Microsoft::DirectXMath PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/../../include\"
)
")
file(WRITE "${_config_dir}/directxmathConfigVersion.cmake"
"set(PACKAGE_VERSION \"2026.06.12\")
if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
")

vcpkg_cmake_config_fixup(CONFIG_PATH share/directxmath)

if(("dx11" IN_LIST FEATURES) OR ("dx12" IN_LIST FEATURES))
    vcpkg_cmake_config_fixup(CONFIG_PATH share/directxsh)
endif()

if("xdsp" IN_LIST FEATURES)
    vcpkg_cmake_config_fixup(CONFIG_PATH share/xdsp)
endif()

if(NOT VCPKG_TARGET_IS_WINDOWS)
    vcpkg_download_distfile(
        SAL_HEADER
        URLS "https://raw.githubusercontent.com/dotnet/runtime/v9.0.2/src/coreclr/pal/inc/rt/sal.h"
        FILENAME "sal.h"
        SHA512 8085f67bfa4ce01ae89461cadf72454a9552fde3f08b2dcc3de36b9830e29ce7a6192800f8a5cb2a66af9637be0017e85719826a4cfdade508ae97f319e0ee8e
    )
    file(INSTALL ${DOWNLOADS}/sal.h DESTINATION ${CURRENT_PACKAGES_DIR}/include)
endif()

if(("dx11" IN_LIST FEATURES) OR ("dx12" IN_LIST FEATURES))
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
else()
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
endif()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

if(("dx11" IN_LIST FEATURES) OR ("dx12" IN_LIST FEATURES))
    file(READ "${CMAKE_CURRENT_LIST_DIR}/shmathusage" USAGE_CONTENT)
    file(APPEND "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" ${USAGE_CONTENT})
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
