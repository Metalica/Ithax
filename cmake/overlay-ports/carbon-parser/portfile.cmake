vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/parser.git
  REF b58a68538fe4ae63b3854b391744b86f561b901b
  HEAD_REF main
)

# The upstream project discovers its host generators with find_program().
vcpkg_add_to_path(PREPEND
  "${CURRENT_HOST_INSTALLED_DIR}/bin"
  "${CURRENT_HOST_INSTALLED_DIR}/tools/lemon"
)

vcpkg_replace_string(
  "${SOURCE_PATH}/CMakeLists.txt"
  [=[        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>]=]
  [=[        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>]=]
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
  ${FEATURE_OPTIONS}
  -DBUILD_TESTING=OFF
  -DVCPKG_USE_HOST_TOOLS=ON
  -DVCPKG_HOST_TRIPLET=${HOST_TRIPLET}
  -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)

vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
vcpkg_cmake_config_fixup()
vcpkg_copy_pdbs()
ccp_externalize_apple_debuginfo()
