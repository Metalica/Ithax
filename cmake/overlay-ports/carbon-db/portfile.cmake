vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/db.git
  REF a2b11c05da7501a46b0c651a3f5c80e3a5228092
  HEAD_REF main
)

string(
  CONCAT
  LEGACY_RUNTIME_ERROR
  "return PyErr_SetString(PyExc_RuntimeError, "
  "\"This tasklet cannot block, and synchronous calls are not allowed\"), "
  "0;"
)
string(
  CONCAT
  FIXED_RUNTIME_ERROR
  "return PyErr_SetString(PyExc_RuntimeError, "
  "\"This tasklet cannot block, and synchronous calls are not allowed\"), "
  "nullptr;"
)
vcpkg_replace_string(
  "${SOURCE_PATH}/NSession.cpp"
  "${LEGACY_RUNTIME_ERROR}"
  "${FIXED_RUNTIME_ERROR}"
)
vcpkg_replace_string(
  "${SOURCE_PATH}/SqlCommand.cpp"
  "return PyErr_SetObject(Utilities::ErrorClass(hr), errorArgs), 0;"
  "return PyErr_SetObject(Utilities::ErrorClass(hr), errorArgs), nullptr;"
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

set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
vcpkg_cmake_config_fixup()
set(BUILD_PATHS
        "${CURRENT_PACKAGES_DIR}/bin/*.pyd"
        "${CURRENT_PACKAGES_DIR}/debug/bin/*.pyd"
)
vcpkg_copy_pdbs(BUILD_PATHS ${BUILD_PATHS})
ccp_externalize_apple_debuginfo()
