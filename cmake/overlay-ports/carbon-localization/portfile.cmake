vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/localization.git
  REF 05da2558de31656a9fbac1c85712646f5d80282c
  HEAD_REF main
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
file(COPY
  "${CMAKE_CURRENT_LIST_DIR}/carbon-localization.cmake"
  "${CMAKE_CURRENT_LIST_DIR}/carbon-localizationConfig.cmake"
  DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
set(BUILD_PATHS
        "${CURRENT_PACKAGES_DIR}/bin/*.pyd"
        "${CURRENT_PACKAGES_DIR}/debug/bin/*.pyd"
)
vcpkg_copy_pdbs(BUILD_PATHS ${BUILD_PATHS})
ccp_externalize_apple_debuginfo()
