vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/resources.git
  REF 677f05a0605f11ec378553c71874a964023e5010
  HEAD_REF main
)

vcpkg_replace_string(
  "${SOURCE_PATH}/CMakeLists.txt"
  "configure_file(src/Version.h.in include/Version.h @ONLY)"
  "configure_file(src/Version.h.in include/CarbonResourcesVersion.h @ONLY)"
)
vcpkg_replace_string(
  "${SOURCE_PATH}/CMakeLists.txt"
  "set(PUBLIC_GENERATED_VERSION_HEADER_FILE include/Version.h)"
  "set(PUBLIC_GENERATED_VERSION_HEADER_FILE include/CarbonResourcesVersion.h)"
)
vcpkg_replace_string(
  "${SOURCE_PATH}/include/Enums.h"
  "#include \"Version.h\""
  "#include \"CarbonResourcesVersion.h\""
)

vcpkg_check_features(
  OUT_FEATURE_OPTIONS RESOURCES_FEATURE_OPTIONS
  FEATURES
    cli BUILD_CLI
    docs BUILD_DOCUMENTATION
    tests BUILD_TESTING
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
    ${RESOURCES_FEATURE_OPTIONS}
    -DVCPKG_USE_HOST_TOOLS=ON
    -DVCPKG_HOST_TRIPLET=${HOST_TRIPLET}
    -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)

vcpkg_cmake_install()
vcpkg_install_copyright(
  FILE_LIST
    "${SOURCE_PATH}/LICENSE.txt"
    "${SOURCE_PATH}/NOTICE.md"
)
vcpkg_cmake_config_fixup()
vcpkg_copy_pdbs()
