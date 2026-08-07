include(CMakeFindDependencyMacro)

find_dependency(carbon-blue CONFIG REQUIRED)
find_dependency(carbon-blueexposure CONFIG REQUIRED)
find_dependency(Python3 COMPONENTS Development REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/carbon-localization.cmake")
