cmake_minimum_required(VERSION 3.16)
project(CarbonAudioStub VERSION 1.0.0)

find_package(carbon-blue CONFIG REQUIRED)
find_package(carbon-trinityaudioapi CONFIG REQUIRED)
find_package(Python3 COMPONENTS Development REQUIRED)

add_library(CarbonAudio SHARED src/stub.cpp)
target_link_libraries(
    CarbonAudio
    PUBLIC Blue CcpTrinityAudioAPI
    PRIVATE Python3::Python
)
set_target_properties(
    CarbonAudio
    PROPERTIES
        OUTPUT_NAME "_audio2"
        DEBUG_POSTFIX "_debug"
        PREFIX ""
        SUFFIX ".pyd"
)
target_compile_definitions(CarbonAudio PUBLIC CARBONAUDIOBUILD)
target_compile_definitions(CarbonAudio PRIVATE CCP_BUILD_FLAVOR=_debug)

install(TARGETS CarbonAudio EXPORT CarbonAudioTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(
    EXPORT CarbonAudioTargets
    DESTINATION share/carbon-audio
    FILE carbon-audio.cmake
)
install(FILES carbon-audioConfig.cmake DESTINATION share/carbon-audio)
