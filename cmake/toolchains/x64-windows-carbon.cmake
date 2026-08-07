# Ithax x64-windows carbon toolchain — carbon x64-windows-carbon.cmake with v143 toolset
# Based on carbonengine/vcpkg-registry/toolchains/x64-windows-carbon.cmake
if (NOT _CCP_TOOLCHAIN_FILE_LOADED)
    set(_CCP_TOOLCHAIN_FILE_LOADED 1)

    set (VCPKG_USE_HOST_TOOLS ON CACHE STRING "")
    set (CMAKE_CXX_STANDARD 17 CACHE STRING "")
    set (CMAKE_CXX_STANDARD_REQUIRED ON CACHE STRING "")
    set (CMAKE_CXX_EXTENSIONS OFF CACHE STRING "")
    set (CMAKE_POSITION_INDEPENDENT_CODE ON CACHE STRING "")
    set (CMAKE_CXX_VISIBILITY_PRESET hidden CACHE STRING "")
    set (CMAKE_OBJCXX_VISIBILITY_PRESET hidden CACHE STRING "")
    set (CMAKE_INTERPROCEDURAL_OPTIMIZATION ON CACHE STRING "")

    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL CACHE STRING INTERNAL FORCE)
    set(CMAKE_SYSTEM_VERSION 10.0.17763.0 CACHE STRING INTERNAL FORCE)
    set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION 10.0.17763.0 CACHE STRING INTERNAL FORCE)

    # Windows 10 minimum
    add_compile_definitions(WINVER=0x0A00)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
    add_compile_definitions(_WIN32_WINDOWS=0x0A00)
    add_compile_definitions(NTDDI_VERSION=0x0A000000)

    set(CCP_PLATFORM Windows CACHE STRING "Target Platform")
    set(CCP_ARCHITECTURE x64 CACHE STRING "Target Architecture")
    set(CCP_TOOLSET v143 CACHE STRING "Target Toolset")

    add_compile_options(/MP)
    add_compile_options(/W3)
    add_compile_options(/permissive-)
    add_link_options(/IGNORE:4099)
    add_link_options(/NODEFAULTLIB:libcmt.lib)

    add_definitions(-D_SBCS)

    set(MATH_OPTIMIZE_FLAG "/fp:fast")

    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
endif ()
