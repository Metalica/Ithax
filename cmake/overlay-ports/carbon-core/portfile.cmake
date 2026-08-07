vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/core.git
  REF b8cbfd6daefe7bc00b1157e3e328efad34e1a239
  HEAD_REF main
)

vcpkg_replace_string(
    "${SOURCE_PATH}/CMakeLists.txt"
    "set(CCP_TOOLSET \${CMAKE_GENERATOR_TOOLSET})"
    "set(CCP_TOOLSET ${VCPKG_PLATFORM_TOOLSET})"
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
      documentation BUILD_DOCUMENTATION
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
  ${FEATURE_OPTIONS}
  -DBUILD_TESTING=OFF
  -DCMAKE_CXX_STANDARD=17
  -DVCPKG_USE_HOST_TOOLS=ON
  -DVCPKG_HOST_TRIPLET=${HOST_TRIPLET}
  -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
vcpkg_copy_pdbs()
if(VCPKG_TARGET_IS_OSX)
  ccp_externalize_apple_debuginfo()
endif()
