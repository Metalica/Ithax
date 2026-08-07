vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/blue.git
  REF 2c7a077d664325f5e66700690683f626dafe06af
  HEAD_REF main
  PATCHES
    fix-python-error-return.patch
    fix-windows-locale-include.patch
)

vcpkg_replace_string(
  "${SOURCE_PATH}/src/win32.cpp"
  "return PyWin32Error(), 0;"
  "return PyWin32Error(), nullptr;"
)
vcpkg_replace_string(
  "${SOURCE_PATH}/src/win32.cpp"
  "return PyErr_SetString( PyExc_NotImplementedError, \"Not available on this platform\" ), NULL;"
  "return PyErr_SetString( PyExc_NotImplementedError, \"Not available on this platform\" ), nullptr;"
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
  -DBUILD_TESTING=OFF
  -DVCPKG_USE_HOST_TOOLS=ON
  -DVCPKG_HOST_TRIPLET=${HOST_TRIPLET}
  -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup()
vcpkg_install_copyright(
  FILE_LIST
    "${SOURCE_PATH}/LICENSE.md"
    "${SOURCE_PATH}/NOTICE.md"
)
set(BUILD_PATHS
    "${CURRENT_PACKAGES_DIR}/bin/*.pyd"
    "${CURRENT_PACKAGES_DIR}/debug/bin/*.pyd"
)
vcpkg_copy_pdbs(BUILD_PATHS ${BUILD_PATHS})
ccp_externalize_apple_debuginfo()
