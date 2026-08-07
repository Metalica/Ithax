vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/trinityaudioapi.git
  REF 0a3a12c8f42e747b5287c7c6cadbaab06bf84786 #v2.0.3 plus MIT license
  HEAD_REF main
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
)

vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST ${SOURCE_PATH}/LICENSE.md)

vcpkg_cmake_config_fixup()
