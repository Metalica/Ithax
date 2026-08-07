# Ithax chainload triplet toolchain — wraps vcpkg windows toolchain + our carbon toolchain
include($ENV{PATH_TO_VCPKG_ROOT}/scripts/toolchains/windows.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../toolchains/x64-windows-carbon.cmake)
