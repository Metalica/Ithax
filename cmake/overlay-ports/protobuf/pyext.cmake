find_package(Python3 COMPONENTS Development REQUIRED)

set(pyext_dir "${CMAKE_CURRENT_SOURCE_DIR}/../python/google/protobuf/pyext")
file(GLOB_RECURSE pyext_files
    LIST_DIRECTORIES false
    "${pyext_dir}/*.cc"
    "${pyext_dir}/*.h"
)
file(GLOB_RECURSE pyext_headers
    LIST_DIRECTORIES false
    "${pyext_dir}/*.h"
)

if(MSVC)
    add_compile_options(/Zc:strictStrings- /wd4244 /wd4267)
endif()

add_library(pyext STATIC ${pyext_files})
target_include_directories(pyext PUBLIC
    "$<BUILD_INTERFACE:${pyext_dir}>"
    "$<BUILD_INTERFACE:${protobuf_source_dir}/python>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(pyext PUBLIC libprotobuf Python3::Python)
