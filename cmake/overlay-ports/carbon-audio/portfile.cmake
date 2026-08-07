vcpkg_cmake_configure(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/stub"
    OPTIONS
    -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
vcpkg_install_copyright(
    FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/../../../LICENSE"
)
vcpkg_copy_pdbs()
