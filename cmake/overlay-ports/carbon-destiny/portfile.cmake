vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/destiny.git
  REF 5d7b818ee3c91f6a5b752189dba1cc86bf148de0
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
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

# Remove headers already installed by carbon-trinity to avoid file conflict
file(REMOVE
    "${CURRENT_PACKAGES_DIR}/include/IEveBallpark.h"
    "${CURRENT_PACKAGES_DIR}/include/IEveReferencePoint.h"
)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include")
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)

vcpkg_cmake_config_fixup()
set(BUILD_PATHS
        "${CURRENT_PACKAGES_DIR}/lib/*.pyd"
        "${CURRENT_PACKAGES_DIR}/debug/lib/*.pyd"
)
vcpkg_copy_pdbs(BUILD_PATHS ${BUILD_PATHS})
ccp_externalize_apple_debuginfo()
