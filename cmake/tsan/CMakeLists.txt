cmake_minimum_required(VERSION 3.21)

project(ithax_stage3_tsan LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The Stage 3 TSan harness requires Linux")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "The Stage 3 TSan harness requires Clang or GCC")
endif()

include(CTest)
find_package(Threads REQUIRED)

set(ITHAX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(ithax-stage3-tsan
    "${ITHAX_ROOT}/src/stage3_tsan.cpp"
    "${ITHAX_ROOT}/src/threading/frame_packet_channel.cpp"
    "${ITHAX_ROOT}/src/threading/frame_slot.cpp"
)

target_compile_features(ithax-stage3-tsan PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-tsan PRIVATE
    "${ITHAX_ROOT}/src")
target_compile_options(
    ithax-stage3-tsan PRIVATE
    -fsanitize=thread
    -fno-omit-frame-pointer
    -g
    -O1)
target_link_options(ithax-stage3-tsan PRIVATE -fsanitize=thread)
target_link_libraries(ithax-stage3-tsan PRIVATE Threads::Threads)

add_test(NAME stage3_linux_tsan COMMAND ithax-stage3-tsan)
set_tests_properties(
    stage3_linux_tsan
    PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:history_size=7"
    TIMEOUT 60)
